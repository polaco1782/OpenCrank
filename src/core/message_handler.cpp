/*
 * OpenCrank C++11 - Message Handler Implementation
 * 
 * Routes incoming messages to commands, skills, or AI.
 */
#include <opencrank/core/message_handler.hpp>
#include <opencrank/core/application.hpp>
#include <opencrank/core/logger.hpp>
#include <opencrank/core/channel.hpp>
#include <opencrank/ai/ai.hpp>

#include <sstream>
#include <ctime>

namespace opencrank {

// ============================================================================
// Plugin Notification
// ============================================================================

void notify_outgoing_message(
    const std::string& channel_id, 
    const std::string& to,
    const std::string& text, 
    const std::string& reply_to) 
{
    auto& app = Application::instance();
    
    // Create pseudo-message for outgoing response
    Message out_msg;
    out_msg.id = "bot-" + std::to_string(std::time(nullptr));
    out_msg.channel = channel_id;
    out_msg.from = "bot";
    out_msg.from_name = "OpenCrank Bot";
    out_msg.to = to;
    out_msg.text = text;
    out_msg.chat_type = "private";
    out_msg.timestamp = std::time(nullptr);
    out_msg.reply_to_id = reply_to;
    
    // Notify all plugins
    for (auto* plugin : app.registry().plugins()) {
        if (plugin && plugin->is_initialized()) {
            plugin->on_incoming_message(out_msg);
        }
    }
    
    LOG_DEBUG("[MessageHandler] ◀ OUT Notified %zu plugins of outgoing response to %s",
              app.registry().plugins().size(), to.c_str());
}

// ============================================================================
// Error Callback
// ============================================================================

void on_error(const std::string& channel, const std::string& error) {
    LOG_ERROR("Channel %s error: %s", channel.c_str(), error.c_str());
}

// ============================================================================
// Message Callback (Entry Point)
// ============================================================================

void on_message(const Message& msg) {
    auto& app = Application::instance();
    
    // Deduplicate
    if (!app.debouncer().should_process(msg.id)) {
        LOG_DEBUG("Skipping duplicate message: %s", msg.id.c_str());
        return;
    }
    
    LOG_INFO("[%s] ▶ IN  Message from %s: %.200s%s", 
             msg.channel.c_str(), msg.from_name.c_str(), msg.text.c_str(),
             msg.text.size() > 200 ? "..." : "");
    
    // Notify all plugins
    for (auto* plugin : app.registry().plugins()) {
        if (plugin && plugin->is_initialized()) {
            plugin->on_incoming_message(msg);
        }
    }
    
    // Rate limit check
    auto rate_result = app.user_limiter().check(msg.from);
    if (!rate_result.allowed) {
        LOG_WARN("Rate limit exceeded for user %s, retry in %lldms", 
                 msg.from.c_str(), static_cast<long long>(rate_result.retry_after_ms));
        return;
    }
    
    // Process in thread pool (non-blocking)
    // Copy msg for the lambda capture
    Message msg_copy = msg;
    app.thread_pool().enqueue([msg_copy]() {
        process_message(msg_copy);
    });
}

// ============================================================================
// Internal Helpers
// ============================================================================

namespace detail {

std::string handle_command(
    const Message& msg,
    Session& session,
    const std::string& cmd_text)
{
    auto& app = Application::instance();
    
    // Check if this is a skill command
    std::string response;
    if (handle_skill_command(msg, session, cmd_text, response)) {
        return response;
    }
    
    // Parse command and arguments
    std::string command, args;
    auto space_pos = cmd_text.find(' ');
    if (space_pos != std::string::npos) {
        command = cmd_text.substr(0, space_pos);
        args = cmd_text.substr(space_pos + 1);
    } else {
        command = cmd_text;
    }
    
    // Handle /skills command
    if (command == "/skills") {
        std::ostringstream oss;
        oss << "**Available Skills:**\n\n";
        oss << app.skills().list_skills_for_display(app.skill_entries(), true);
        oss << "\n💡 Use `/skillname <args>` or `/skill skillname <args>` to invoke a skill";
        return oss.str();
    }
    
    // Route to registered command handler
    return app.registry().execute_command(command, msg, session, args);
}

bool handle_skill_command(
    const Message& msg,
    Session& session,
    const std::string& cmd_text,
    std::string& response_out)
{
    auto& app = Application::instance();
    
    // Check if this is a skill command
    auto skill_cmd = app.skills().resolve_skill_command_invocation(cmd_text, app.skill_commands());
    
    LOG_DEBUG("[Skills] Skill command resolution result: %s", 
              skill_cmd.first ? "MATCHED" : "NO MATCH");
    
    if (!skill_cmd.first) {
        return false;
    }
    
    // This is a skill command!
    const auto* spec = skill_cmd.first;
    const auto& skill_args = skill_cmd.second;
    
    LOG_INFO("[Skills] ▶ IN  Skill command invoked: %s (args: %s)", spec->skill_name.c_str(), skill_args.c_str());
    
    if (!spec->dispatch.empty() && spec->dispatch.kind == "tool") {
        // Direct tool dispatch - not implemented yet
        response_out = "⚠️ Direct tool dispatch not yet implemented for skill '" + spec->skill_name + "'";
        LOG_WARN("Direct tool dispatch requested but not implemented");
        return true;
    }
    
    // AI-mediated dispatch via agentic loop
    LOG_DEBUG("[Skills] AI-mediated skill dispatch via agentic loop: %s", spec->skill_name.c_str());
    
    // Find the skill entry
    const auto* entry = app.skills().find_skill_by_name(spec->skill_name, app.skill_entries());
    LOG_DEBUG("[Skills] Skill entry lookup: %s", entry ? "FOUND" : "NOT FOUND");
    
    if (!entry) {
        response_out = "⚠️ Skill '" + spec->skill_name + "' not found in loaded entries";
        LOG_WARN("Skill command matched but skill entry not found: %s", spec->skill_name.c_str());
        return true;
    }
    
    // Rewrite prompt
    std::string rewritten = "Use the '" + spec->skill_name + "' skill. ";
    rewritten += "First, read its instructions using the read tool: ";
    rewritten += entry->skill.file_path + ". ";
    rewritten += "Then follow those instructions to complete this task: ";
    rewritten += skill_args;
    
    LOG_DEBUG("[Skills] Rewritten prompt: %s", rewritten.c_str());
    LOG_DEBUG("[Skills] System prompt size: %zu chars", app.system_prompt().size());
    
    // Route to AI
    auto* ai = app.registry().get_default_ai();
    LOG_DEBUG("[Skills] AI provider: %s", 
              ai ? (ai->is_configured() ? "READY" : "NOT CONFIGURED") : "NULL");
    
    if (!ai || !ai->is_configured()) {
        response_out = "⚠️ AI not configured for skill execution. Check your config.json.";
        LOG_WARN("Cannot execute skill - AI not configured");
        return true;
    }
    
    // Start AI monitoring session
    std::string monitor_session_id = msg.channel + ":" + msg.to;
    app.ai_monitor().start_session(monitor_session_id, msg.channel, msg.to);
    
    app.typing().start_typing(msg.to);
    
    LOG_INFO("[Skills] Starting agentic loop for skill: %s", spec->skill_name.c_str());
    
    // Use agent config from application (loaded from config file)
    AgentConfig agent_config = app.agent().config();
    
    auto agent_result = app.agent().run(
        ai, 
        rewritten, 
        session.history(), 
        app.system_prompt(),
        agent_config,
        msg.channel,
        msg.to
    );
    
    LOG_DEBUG("[Skills] Agent loop completed: success=%s, iterations=%d, tool_calls=%d",
              agent_result.success ? "yes" : "no", 
              agent_result.iterations, 
              agent_result.tool_calls_made);
    
    if (agent_result.success) {
        response_out = agent_result.final_response;
        LOG_INFO("[Skills] ◀ OUT Skill completed via %d tool calls (response: %zu chars)", 
                 agent_result.tool_calls_made, response_out.size());
    } else {
        response_out = "❌ Skill execution failed: " + agent_result.error;
        LOG_ERROR("[Skills] ◀ OUT Skill execution failed: %s", agent_result.error.c_str());
    }
    
    app.typing().stop_typing(msg.to);
    app.ai_monitor().end_session(monitor_session_id);
    return true;
}

std::string handle_ai_message(
    const Message& msg,
    Session& session)
{
    auto& app = Application::instance();
    
    auto* ai = app.registry().get_default_ai();
    if (!ai || !ai->is_configured()) {
        return "No AI provider configured. Set in config.json to enable AI features. "
               "Type /help for available commands.";
    }
    
    // Start AI monitoring session
    std::string monitor_session_id = msg.channel + ":" + msg.to;
    app.ai_monitor().start_session(monitor_session_id, msg.channel, msg.to);
    
    // Start typing indicator
    app.typing().start_typing(msg.to);
    
    LOG_DEBUG("[AI] === ▶ IN  User message entering agentic loop ===");
    LOG_DEBUG("[AI] ▶ IN  User: %s", msg.from_name.c_str());
    LOG_DEBUG("[AI] ▶ IN  Channel: %s, To: %s", msg.channel.c_str(), msg.to.c_str());
    LOG_DEBUG("[AI] ▶ IN  Message (%zu chars): %.200s%s", 
              msg.text.size(), msg.text.c_str(),
              msg.text.size() > 200 ? "..." : "");
    LOG_DEBUG("[AI] Session history size: %zu messages", session.history().size());
    LOG_DEBUG("[AI] System prompt length: %zu chars (~%zu tokens)", 
              app.system_prompt().size(), app.system_prompt().size() / 4);
    LOG_DEBUG("[AI] Registered tools: %zu", app.agent().tools().size());
    
    // Run agentic loop with heartbeat callbacks
    // Use agent config from application (loaded from config file)
    AgentConfig agent_config = app.agent().config();
    
    auto agent_result = app.agent().run(
        ai, 
        msg.text, 
        session.history(), 
        app.system_prompt(),
        agent_config,
        msg.channel,
        msg.to
    );
    
    LOG_DEBUG("[AI] === ◀ OUT Agent loop complete ===");
    LOG_DEBUG("[AI] ◀ OUT Success: %s, Paused: %s", 
              agent_result.success ? "yes" : "no",
              agent_result.paused ? "yes" : "no");
    LOG_DEBUG("[AI] ◀ OUT Iterations: %d, Tool calls: %d", 
              agent_result.iterations, agent_result.tool_calls_made);
    LOG_DEBUG("[AI] ◀ OUT Response (%zu chars): %.200s%s", 
              agent_result.final_response.size(),
              agent_result.final_response.c_str(),
              agent_result.final_response.size() > 200 ? "..." : "");
    
    std::string response;
    if (agent_result.paused) {
        // Store paused state in session for /continue command
        // This handles both max_iterations and max_consecutive_errors pauses
        session.set_data("agent_paused", "true");
        session.set_data("agent_iterations", std::to_string(agent_result.iterations));
        session.set_data("agent_tool_calls", std::to_string(agent_result.tool_calls_made));
        
        response = agent_result.pause_message;
        LOG_INFO("[AI] Agent paused after %d iterations (%d tool calls), awaiting /continue", 
                 agent_result.iterations, agent_result.tool_calls_made);
    } else if (agent_result.success) {
        // Clear any paused state
        session.remove_data("agent_paused");
        session.remove_data("agent_iterations");
        session.remove_data("agent_tool_calls");
        
        response = agent_result.final_response;
        
        // Log tools used
        if (!agent_result.tools_used.empty()) {
            std::ostringstream tools_str;
            for (size_t i = 0; i < agent_result.tools_used.size(); ++i) {
                if (i > 0) tools_str << ", ";
                tools_str << agent_result.tools_used[i];
            }
            LOG_INFO("[AI] Tools used: %s", tools_str.str().c_str());
        }
    } else {
        // Clear paused state on error
        session.remove_data("agent_paused");
        session.remove_data("agent_iterations");
        session.remove_data("agent_tool_calls");
        
        response = "❌ Agent error: " + agent_result.error;
    }
    
    // Stop typing and end monitoring session
    app.typing().stop_typing(msg.to);
    app.ai_monitor().end_session(monitor_session_id);
    
    return response;
}

void send_response(
    const Message& original_msg,
    const std::string& response)
{
    if (response.empty()) {
        return;
    }
    
    auto& app = Application::instance();
    
    // Route message to ALL registered channels
    const auto& channels = app.registry().channels();
    
    if (channels.empty()) {
        LOG_ERROR("No channels registered");
        return;
    }
    
    // Split into chunks if needed
    auto chunks = split_message_chunks(response, 3500);
    
    for (auto* channel : channels) {
        if (!channel) continue;
        
        std::string channel_id = channel->channel_id();
        
        for (size_t i = 0; i < chunks.size(); ++i) {
            const auto& chunk = chunks[i];
            if (chunk.empty()) {
                continue;
            }

            SendResult result;
            if (i == 0) {
                // Reply to original message on first chunk
                result = channel->send_message(original_msg.to, chunk, original_msg.id);
            } else {
                result = channel->send_message(original_msg.to, chunk);
            }

            if (result.success) {
                LOG_DEBUG("[MessageHandler] ◀ OUT Sent to %s (msg_id=%s, chunk %zu/%zu)", 
                          channel_id.c_str(), result.message_id.c_str(),
                          i + 1, chunks.size());
                notify_outgoing_message(channel_id, original_msg.to, chunk, original_msg.id);
            } else {
                LOG_ERROR("Failed to send response to %s: %s", channel_id.c_str(), result.error.c_str());
                
                // Check thread pool status
                auto pending = app.thread_pool().pending();
                if (pending > 4) {
                    LOG_WARN("Thread pool has %zu pending tasks - system may be overloaded", pending);
                }
                break;
            }
        }
    }
}

} // namespace detail

// ============================================================================
// Main Message Processor
// ============================================================================

void process_message(const Message& msg) {
    auto& app = Application::instance();
    
    LOG_DEBUG("[AI] ▶ IN  Processing message from %s: %.100s%s", 
              msg.from_name.c_str(), msg.text.c_str(),
              msg.text.size() > 100 ? "..." : "");
    
    auto* channel = app.registry().get_channel(msg.channel);
    if (!channel || msg.text.empty()) {
        return;
    }
    
    // Get session
    auto& session = app.sessions().get_session_for_message(msg);
    
    std::string response;
    
    // Handle commands (start with /)
    if (msg.text[0] == '/') {
        // Parse command text (handle @botname suffix)
        std::string cmd_text = msg.text;
        
        auto at_pos = cmd_text.find('@');
        if (at_pos != std::string::npos) {
            auto space_pos = cmd_text.find(' ');
            if (space_pos == std::string::npos || at_pos < space_pos) {
                cmd_text = cmd_text.substr(0, at_pos) + 
                          (space_pos != std::string::npos ? cmd_text.substr(space_pos) : "");
            }
        }
        
        response = detail::handle_command(msg, session, cmd_text);
        
        // Unknown command - don't respond
        if (response.empty()) {
            return;
        }
    } else {
        // Regular message - route to AI
        response = detail::handle_ai_message(msg, session);
    }
    
    // Send response
    detail::send_response(msg, response);
}

} // namespace opencrank
