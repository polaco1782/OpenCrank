/*
 * opencrank C++ - Context Manager
 * 
 * Manages AI context window intelligently:
 * - Monitors context usage and detects when approaching limits (75%)
 * - Generates conversation resumes to preserve continuity
 * - Saves resumes to persistent memory
 * - Wipes context and reloads memory for fresh continuation
 * 
 * This replaces the old "chop messages" approach with a smart
 * resume-based context management strategy.
 */
#ifndef opencrank_CORE_CONTEXT_MANAGER_HPP
#define opencrank_CORE_CONTEXT_MANAGER_HPP

#include <opencrank/core/json.hpp>
#include <opencrank/ai/ai.hpp>
#include <string>
#include <vector>

namespace opencrank {

// Forward declarations
class MemoryTool;

// ============================================================================
// Context Manager Configuration
// ============================================================================

struct ContextManagerConfig {
    double usage_threshold;          // Trigger resume at this % (default: 0.75)
    size_t max_context_chars;        // Total context budget in characters
    size_t reserve_for_response;     // Characters reserved for AI response
    size_t max_resume_chars;         // Maximum resume size in characters
    bool auto_save_memory;           // Auto-save resume to memory (default: true)
    
    ContextManagerConfig()
        : usage_threshold(0.75)
        , max_context_chars(16000)
        , reserve_for_response(4000)
        , max_resume_chars(3000)
        , auto_save_memory(true) {}
};

// ============================================================================
// Context Usage Info
// ============================================================================

struct ContextUsage {
    size_t system_prompt_chars;      // System prompt size
    size_t history_chars;            // All messages size
    size_t total_chars;              // Total estimated characters
    size_t budget_chars;             // Available budget
    double usage_ratio;              // 0.0 to 1.0+
    bool needs_resume;               // True if usage >= threshold
    
    ContextUsage()
        : system_prompt_chars(0)
        , history_chars(0)
        , total_chars(0)
        , budget_chars(0)
        , usage_ratio(0.0)
        , needs_resume(false) {}
};

// ============================================================================
// Context Manager
// ============================================================================

class ContextManager {
public:
    ContextManager();
    ~ContextManager();
    
    // Configure the manager
    void set_config(const ContextManagerConfig& config);
    const ContextManagerConfig& config() const { return config_; }
    
    // Set memory tool for persistence (optional)
    void set_memory_tool(MemoryTool* tool) { memory_tool_ = tool; }
    
    // Estimate current context usage
    ContextUsage estimate_usage(
        const std::vector<ConversationMessage>& history,
        const std::string& system_prompt) const;
    
    // Check if context needs a resume cycle
    bool needs_resume(
        const std::vector<ConversationMessage>& history,
        const std::string& system_prompt) const;
    
    // Perform the full resume cycle:
    // 1. Ask AI to generate a resume of the conversation
    // 2. Save the resume to persistent memory
    // 3. Wipe the conversation history
    // 4. Inject the resume as context for continuation
    //
    // Returns true if the cycle was performed successfully.
    // The history vector is modified in-place.
    bool perform_resume_cycle(
        AIPlugin* ai,
        std::vector<ConversationMessage>& history,
        const std::string& system_prompt,
        const std::string& session_key = "");
    
    // Generate a conversation resume using the AI
    // Returns empty string on failure
    std::string generate_resume(
        AIPlugin* ai,
        const std::vector<ConversationMessage>& history,
        const std::string& system_prompt) const;
    
    // Save a resume to persistent memory via MemoryTool
    bool save_resume_to_memory(
        const std::string& resume,
        const std::string& session_key = "");
    
    // Load the most recent resume from memory
    std::string load_resume_from_memory(
        const std::string& session_key = "") const;
    
    // Build a fresh history with resume context injected
    std::vector<ConversationMessage> build_resumed_history(
        const std::string& resume,
        const std::string& last_user_message,
        const std::string& system_prompt) const;

private:
    ContextManagerConfig config_;
    MemoryTool* memory_tool_;
    
    // Estimate character count for a set of messages
    size_t estimate_chars(const std::vector<ConversationMessage>& messages) const;
    
    // Build the resume generation prompt
    std::string build_resume_prompt() const;
};

} // namespace opencrank

#endif // opencrank_CORE_CONTEXT_MANAGER_HPP
