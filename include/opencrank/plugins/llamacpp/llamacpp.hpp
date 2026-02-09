/*
 * opencrank C++11 - Llama.cpp AI Plugin
 * 
 * Implementation of the llama.cpp server API.
 * Uses the OpenAI-compatible API (http://localhost:8080/v1/chat/completions).
 * 
 * Config:
 *   llamacpp.url          - Server URL (default: http://localhost:8080)
 *   llamacpp.model        - Model name (optional)
 *   llamacpp.api_key      - API key if server requires authentication (optional)
 */
#ifndef opencrank_PLUGINS_LLAMACPP_HPP
#define opencrank_PLUGINS_LLAMACPP_HPP

#include <opencrank/ai/ai.hpp>
#include <opencrank/core/http_client.hpp>
#include <opencrank/core/context_manager.hpp>
#include <opencrank/core/logger.hpp>
#include <opencrank/core/config.hpp>
#include <sstream>

namespace opencrank {

class LlamaCppAI : public AIPlugin {
public:
    LlamaCppAI();
    
    // Plugin interface
    const char* name() const;
    const char* version() const;
    const char* description() const;
    
    bool init(const Config& cfg);
    void shutdown();
    bool is_initialized() const;
    
    // AIPlugin interface
    std::string provider_id() const;
    std::vector<std::string> available_models() const;
    std::string default_model() const;
    bool is_configured() const;
    
    // Single prompt completion
    CompletionResult complete(
        const std::string& prompt,
        const CompletionOptions& opts = CompletionOptions()
    );
    
    // Conversation completion
    CompletionResult chat(
        const std::vector<ConversationMessage>& messages,
        const CompletionOptions& opts = CompletionOptions()
    );
    
    // Convenience method: simple question-answer
    std::string ask(const std::string& question, const std::string& system = "");
    
    // Convenience method: continue a conversation
    std::string reply(
        std::vector<ConversationMessage>& history,
        const std::string& user_message,
        const std::string& system = ""
    );

    // Access the context manager for external configuration
    ContextManager& context_manager() { return context_manager_; }
    const ContextManager& context_manager() const { return context_manager_; }

private:
    std::string server_url_;
    std::string api_key_;
    std::string default_model_;
    size_t max_context_chars_;   // Approx char limit for context (chars ≈ tokens * 4)
    bool initialized_;
    ContextManager context_manager_;
    
    // Estimate total character count of a request to proactively avoid context overflow
    size_t estimate_request_chars(const std::vector<ConversationMessage>& messages,
                                  const std::string& system_prompt) const;
    
    // Manage context window intelligently using resume-based strategy.
    // If context is within budget, returns messages unchanged.
    // If context exceeds threshold, performs a resume cycle: generates a summary,
    // saves to memory, wipes history, and returns fresh context with resume.
    std::vector<ConversationMessage> manage_context(
        const std::vector<ConversationMessage>& messages,
        const std::string& system_prompt);
};

} // namespace opencrank

#endif // opencrank_PLUGINS_LLAMACPP_HPP
