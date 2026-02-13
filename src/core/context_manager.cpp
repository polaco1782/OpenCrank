/*
 * OpenCrank C++ - Context Manager Implementation
 * 
 * Intelligent context window management with resume-based strategy.
 * Instead of blindly truncating messages, the manager detects when
 * context is approaching capacity and orchestrates a resume cycle:
 *   1. AI generates a summary/resume of the conversation so far
 *   2. The resume is saved to persistent memory (SQLite)
 *   3. The conversation history is wiped
 *   4. The resume is injected as context for seamless continuation
 */
#include <opencrank/core/context_manager.hpp>
#include <opencrank/core/memory_tool.hpp>
#include <opencrank/core/logger.hpp>

#include <sstream>
#include <ctime>
#include <iomanip>

namespace opencrank {

// ============================================================================
// ContextManager Implementation
// ============================================================================

ContextManager::ContextManager()
    : memory_tool_(nullptr)
{}

ContextManager::~ContextManager() {}

void ContextManager::set_config(const ContextManagerConfig& config) {
    config_ = config;
}

size_t ContextManager::estimate_chars(const std::vector<ConversationMessage>& messages) const {
    size_t total = 0;
    for (size_t i = 0; i < messages.size(); ++i) {
        total += messages[i].content.size();
        total += 20; // Role tags, formatting overhead per message
    }
    return total;
}

ContextUsage ContextManager::estimate_usage(
    const std::vector<ConversationMessage>& history,
    const std::string& system_prompt) const
{
    ContextUsage usage;
    usage.system_prompt_chars = system_prompt.size();
    usage.history_chars = estimate_chars(history);
    usage.total_chars = usage.system_prompt_chars + usage.history_chars;
    usage.budget_chars = config_.max_context_chars - config_.reserve_for_response;
    
    if (usage.budget_chars > 0) {
        usage.usage_ratio = static_cast<double>(usage.total_chars) / 
                            static_cast<double>(usage.budget_chars);
    } else {
        usage.usage_ratio = 1.0;
    }
    
    usage.needs_resume = usage.usage_ratio >= config_.usage_threshold;
    
    return usage;
}

bool ContextManager::needs_resume(
    const std::vector<ConversationMessage>& history,
    const std::string& system_prompt) const
{
    ContextUsage usage = estimate_usage(history, system_prompt);
    return usage.needs_resume;
}

std::string ContextManager::build_resume_prompt() const {
    return 
        "You are about to run out of context window space. Your task now is to create "
        "a RESUME of everything that has happened in this conversation. This resume will "
        "capture the essence of the conversation that will be used to restore your memory "
        "after the context is cleared.\n\n"
        "The resume MUST include:\n"
        "1. **Your original instructions and role** - What system prompt/personality you were given\n"
        "2. **What the user asked for** - The original request and any follow-up requests\n"
        "3. **What you did** - Brief overview of tools called, actions taken, results obtained\n"
        "4. **Current state** - Where you are in the task, what's pending\n"
        "5. **Important facts** - Any key information, file paths, URLs, names mentioned\n"
        "6. **What to do next** - Clear instructions for continuing the task\n\n"
        "What to avoid in the resume:\n"
        "- Do NOT include irrelevant chit-chat or pleasantries\n"
        "- Do NOT include any content that can be easily re-read from the conversation (e.g., simple acknowledgments)\n"
        "- Do NOT include parameters used on the tools"
        "Write the resume as a structured document. Be comprehensive but concise. "
        "Do NOT use any tools. Just output the resume text directly.";
}

std::string ContextManager::generate_resume(
    AIPlugin* ai,
    const std::vector<ConversationMessage>& history,
    const std::string& system_prompt) const
{
    if (!ai || !ai->is_configured()) {
        LOG_ERROR("[ContextManager] Cannot generate resume: AI not available");
        return "";
    }
    
    // Build a request that includes the full history and asks for a resume
    std::vector<ConversationMessage> resume_messages = history;
    
    // Add the resume generation instruction as a user message
    resume_messages.push_back(ConversationMessage::user(build_resume_prompt()));
    
    // Use a minimal system prompt for the resume generation
    CompletionOptions opts;
    opts.system_prompt = system_prompt;
    opts.max_tokens = 2048; // Resume should be concise
    opts.temperature = 0.3; // Low temperature for accuracy
    opts.skip_context_management = true; // Don't manage context during resume generation
    
    LOG_INFO("[ContextManager] Generating conversation resume (%zu messages in history)",
             history.size());
    
    CompletionResult result = ai->chat(resume_messages, opts);
    
    if (!result.success) {
        LOG_ERROR("[ContextManager] Failed to generate resume: %s", result.error.c_str());
        return "";
    }
    
    std::string resume = result.content;
    
    // Truncate if too long
    if (resume.size() > config_.max_resume_chars) {
        resume.resize(config_.max_resume_chars);
        resume += "\n\n[Resume truncated due to size limits]";
        LOG_WARN("[ContextManager] Resume truncated from %zu to %zu chars",
                 result.content.size(), resume.size());
    }
    
    LOG_INFO("[ContextManager] Generated resume: %zu chars", resume.size());
    return resume;
}

bool ContextManager::save_resume_to_memory(
    const std::string& resume,
    const std::string& session_key)
{
    if (!memory_tool_ || !memory_tool_->is_initialized()) {
        LOG_WARN("[ContextManager] Memory tool not available, resume not saved to persistent storage");
        return false;
    }
    
    // Build a memory entry with timestamp and session context
    std::ostringstream content;
    
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    
    content << "# Context Resume\n\n";
    content << "**Generated:** " << time_buf << "\n";
    if (!session_key.empty()) {
        content << "**Session:** " << session_key << "\n";
    }
    content << "\n---\n\n";
    content << resume;
    
    // Save to daily file on disk
    Json file_params;
    file_params["content"] = content.str();
    file_params["daily"] = true;
    file_params["append"] = true;
    
    ToolResult file_result = memory_tool_->execute("file_save", file_params);
    if (file_result.success) {
        LOG_INFO("[ContextManager] Resume saved to daily file");
    } else {
        LOG_ERROR("[ContextManager] Failed to save resume file: %s", file_result.error.c_str());
    }
    
    // Also save to structured database
    Json db_params;
    db_params["content"] = content.str();
    db_params["category"] = "resume";
    db_params["importance"] = 8;
    db_params["tags"] = "context,resume,session";
    
    ToolResult db_result = memory_tool_->execute("memory_save", db_params);
    if (db_result.success) {
        LOG_INFO("[ContextManager] Resume saved to database");
    } else {
        LOG_ERROR("[ContextManager] Failed to save resume to database: %s", db_result.error.c_str());
    }
    
    return file_result.success || db_result.success;
}

std::string ContextManager::load_resume_from_memory(
    const std::string& session_key) const
{
    if (!memory_tool_ || !memory_tool_->is_initialized()) {
        return "";
    }
    
    // Search for recent resumes in the database
    Json params;
    params["query"] = "context resume " + session_key;
    params["max_results"] = 1;
    
    ToolResult result = memory_tool_->execute("memory_search", params);
    
    if (result.success && result.data.contains("memories") && 
        result.data["memories"].is_array() && !result.data["memories"].empty()) {
        
        const Json& first_result = result.data["memories"][0];
        if (first_result.contains("content") && first_result["content"].is_string()) {
            return first_result["content"].get<std::string>();
        }
    }
    
    return "";
}

std::vector<ConversationMessage> ContextManager::build_resumed_history(
    const std::string& resume,
    const std::string& last_user_message,
    const std::string& system_prompt) const
{
    std::vector<ConversationMessage> fresh_history;
    
    // Add the system prompt as the first message
    if (!system_prompt.empty()) {
        fresh_history.push_back(ConversationMessage::system(system_prompt));
    }
    
    // Inject the resume as a system-like context message
    std::ostringstream resume_context;
    resume_context << "[CONTEXT RESUME - Previous conversation was cleared to free up context space. "
                   << "Below is a summary of everything that happened before this point.]\n\n"
                   << resume
                   << "\n\n[END CONTEXT RESUME - Continue from where you left off. "
                   << "You have a fresh context window now.]";
    
    // Add as a user message (since some providers don't support multiple system messages)
    fresh_history.push_back(ConversationMessage::user(resume_context.str()));
    
    // Add an acknowledgment from the assistant
    fresh_history.push_back(ConversationMessage::assistant(
        "Understood. I've reviewed the context resume and I'm ready to continue where we left off."
    ));
    
    // Add the last user message if provided (to maintain conversational flow)
    if (!last_user_message.empty()) {
        fresh_history.push_back(ConversationMessage::user(last_user_message));
    }
    
    return fresh_history;
}

bool ContextManager::perform_resume_cycle(
    AIPlugin* ai,
    std::vector<ConversationMessage>& history,
    const std::string& system_prompt,
    const std::string& session_key)
{
    LOG_INFO("[ContextManager] ═══════════════════════════════════════");
    LOG_INFO("[ContextManager] Starting context resume cycle");
    
    ContextUsage usage = estimate_usage(history, system_prompt);
    LOG_INFO("[ContextManager] Current usage: %.1f%% (%zu/%zu chars, %zu messages)",
             usage.usage_ratio * 100.0, usage.total_chars, usage.budget_chars,
             history.size());
    
    // Step 1: Generate resume
    LOG_INFO("[ContextManager] Step 1: Generating conversation resume...");
    std::string resume = generate_resume(ai, history, system_prompt);
    
    if (resume.empty()) {
        LOG_ERROR("[ContextManager] Failed to generate resume, aborting cycle");
        return false;
    }
    
    // Step 2: Save resume to persistent memory
    if (config_.auto_save_memory) {
        LOG_INFO("[ContextManager] Step 2: Saving resume to persistent memory...");
        save_resume_to_memory(resume, session_key);
    } else {
        LOG_DEBUG("[ContextManager] Step 2: Skipping memory save (auto_save_memory=false)");
    }
    
    // Step 3: Find the last user message before wiping
    std::string last_user_message;
    for (size_t i = history.size(); i > 0; --i) {
        if (history[i - 1].role == MessageRole::USER) {
            // Skip tool result messages
            if (history[i - 1].content.find("[TOOL_RESULT") == std::string::npos) {
                last_user_message = history[i - 1].content;
                break;
            }
        }
    }
    
    // Step 4: Wipe history and inject resume
    LOG_INFO("[ContextManager] Step 3: Wiping context (%zu messages) and injecting resume...",
             history.size());
    
    history = build_resumed_history(resume, last_user_message, system_prompt);
    
    ContextUsage new_usage = estimate_usage(history, system_prompt);
    LOG_INFO("[ContextManager] Context resumed: %.1f%% usage (%zu/%zu chars, %zu messages)",
             new_usage.usage_ratio * 100.0, new_usage.total_chars, new_usage.budget_chars,
             history.size());
    LOG_INFO("[ContextManager] ═══════════════════════════════════════");
    
    return true;
}

} // namespace opencrank
