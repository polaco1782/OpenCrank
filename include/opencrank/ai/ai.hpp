/*
 * opencrank C++11 - AI Plugin Interface
 * 
 * Abstract interface for AI/LLM providers.
 * Supports conversation history, system prompts, and streaming.
 */
#ifndef opencrank_AI_AI_HPP
#define opencrank_AI_AI_HPP

#include "../core/plugin.hpp"
#include "../core/json.hpp"
#include <string>
#include <vector>
#include <functional>

namespace opencrank {

// Message role in a conversation
enum class MessageRole {
    SYSTEM,
    USER,
    ASSISTANT
};

std::string role_to_string(MessageRole role);
MessageRole string_to_role(const std::string& str);

// A message in a conversation
struct ConversationMessage {
    MessageRole role;
    std::string content;
    
    ConversationMessage() : role(MessageRole::USER) {}
    ConversationMessage(MessageRole r, const std::string& c) : role(r), content(c) {}
    
    static ConversationMessage system(const std::string& content) {
        return ConversationMessage(MessageRole::SYSTEM, content);
    }
    static ConversationMessage user(const std::string& content) {
        return ConversationMessage(MessageRole::USER, content);
    }
    static ConversationMessage assistant(const std::string& content) {
        return ConversationMessage(MessageRole::ASSISTANT, content);
    }
};

// Usage stats from API response
struct UsageStats {
    int input_tokens;
    int output_tokens;
    int total_tokens;
    
    UsageStats() : input_tokens(0), output_tokens(0), total_tokens(0) {}
};

// Result of an AI completion request
struct CompletionResult {
    bool success;
    std::string content;      // The AI's response text
    std::string error;        // Error message if failed
    std::string stop_reason;  // Why the model stopped (end_turn, max_tokens, etc.)
    std::string model;        // Model that was used
    UsageStats usage;
    
    static CompletionResult ok(const std::string& text) {
        CompletionResult r;
        r.success = true;
        r.content = text;
        return r;
    }
    
    static CompletionResult fail(const std::string& error) {
        CompletionResult r;
        r.success = false;
        r.error = error;
        return r;
    }
};

// Callback for streaming responses
typedef std::function<void(const std::string& chunk)> StreamCallback;

// AI completion options
struct CompletionOptions {
    std::string model;           // Model to use (empty = provider default)
    std::string system_prompt;   // System prompt/instructions
    int max_tokens;              // Max tokens to generate (0 = default)
    double temperature;          // Sampling temperature (0-1)
    bool stream;                 // Enable streaming
    StreamCallback on_chunk;     // Called for each chunk when streaming
    
    CompletionOptions() 
        : max_tokens(4096), temperature(0.7), stream(false) {}
};

// Abstract AI provider plugin interface
class AIPlugin : public Plugin {
public:
    virtual ~AIPlugin() {}
    
    // Get the provider identifier (e.g., "claude", "openai")
    virtual std::string provider_id() const = 0;
    
    // Get available models for this provider
    virtual std::vector<std::string> available_models() const = 0;
    
    // Get the default model
    virtual std::string default_model() const = 0;
    
    // Send a single prompt and get a response
    virtual CompletionResult complete(
        const std::string& prompt,
        const CompletionOptions& opts = CompletionOptions()
    ) = 0;
    
    // Send a conversation (with history) and get a response
    virtual CompletionResult chat(
        const std::vector<ConversationMessage>& messages,
        const CompletionOptions& opts = CompletionOptions()
    ) = 0;
    
    // Check if the provider is properly configured
    virtual bool is_configured() const = 0;
    
    // Handle an incoming chat message (adds to session, calls AI, returns response)
    // Returns the AI response text, or error message prefixed with error emoji
    virtual std::string handle_message(const std::string& user_text,
                                       std::vector<ConversationMessage>& history,
                                       const std::string& system_prompt = "") {
        // Default implementation - can be overridden
        if (!is_configured()) {
            return "🤖 AI not configured.";
        }
        
        history.push_back(ConversationMessage::user(user_text));
        
        CompletionOptions opts;
        opts.system_prompt = system_prompt;
        opts.max_tokens = 4096;  // Increased from 1024 to handle longer responses
        
        CompletionResult result = chat(history, opts);
        
        if (result.success) {
            history.push_back(ConversationMessage::assistant(result.content));
            return result.content;
        }
        
        // Remove failed user message
        history.pop_back();
        return "❌ AI error: " + result.error;
    }
};

} // namespace opencrank

#endif // opencrank_AI_AI_HPP
