/*
 * opencrank C++ - Claude AI Plugin
 * 
 * Implementation of the Anthropic Claude API.
 * Uses the Messages API (https://api.anthropic.com/v1/messages).
 * 
 * Config:
 *   ai.api_key    - Your Anthropic API key
 *   ai.model      - Default model (optional, defaults to claude-sonnet-4-20250514)
 */
#ifndef opencrank_PLUGINS_CLAUDE_HPP
#define opencrank_PLUGINS_CLAUDE_HPP

#include <opencrank/ai/ai.hpp>
#include <opencrank/core/http_client.hpp>
#include <opencrank/core/logger.hpp>
#include <opencrank/core/config.hpp>
#include <sstream>
#include <cstdlib>

namespace opencrank {

class ClaudeAI : public AIPlugin {
public:
    ClaudeAI();
    
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

private:
    std::string api_key_;
    std::string default_model_;
    std::string api_url_;
    std::string api_version_;
    bool initialized_;
};

} // namespace opencrank

#endif // opencrank_AI_CLAUDE_HPP
