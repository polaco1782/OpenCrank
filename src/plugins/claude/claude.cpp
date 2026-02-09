#include <opencrank/plugins/claude/claude.hpp>
#include <opencrank/core/loader.hpp>
#include <sstream>

namespace opencrank {

ClaudeAI::ClaudeAI()
    : api_key_()
    , default_model_("claude-sonnet-4-20250514")
    , api_url_("https://api.anthropic.com/v1/messages")
    , api_version_("2023-06-01")
    , initialized_(false)
{}

const char* ClaudeAI::name() const { return "Claude AI"; }
const char* ClaudeAI::version() const { return "1.0.0"; }
const char* ClaudeAI::description() const { 
    return "Anthropic Claude AI provider using Messages API"; 
}

bool ClaudeAI::init(const Config& cfg) {
    api_key_ = cfg.get_string("claude.api_key", "");
    
    std::string model = cfg.get_string("claude.model", "");
    if (!model.empty()) {
        default_model_ = model;
    }
    
    std::string url = cfg.get_string("claude.api_url", "");
    if (!url.empty()) {
        api_url_ = url;
    }
    
    if (api_key_.empty()) {
        LOG_WARN("Claude AI: No API key configured (set claude.api_key in config.json)");
        initialized_ = false;
        return false;
    }
    
    LOG_INFO("Claude AI initialized with model: %s", default_model_.c_str());
    initialized_ = true;
    return true;
}

void ClaudeAI::shutdown() {
    initialized_ = false;
}

bool ClaudeAI::is_initialized() const { return initialized_; }

std::string ClaudeAI::provider_id() const { return "claude"; }

std::vector<std::string> ClaudeAI::available_models() const {
    std::vector<std::string> models;
    models.push_back("claude-opus-4-20250514");
    models.push_back("claude-sonnet-4-20250514");
    models.push_back("claude-haiku-3-5-20241022");
    models.push_back("claude-3-opus-20240229");
    models.push_back("claude-3-sonnet-20240229");
    models.push_back("claude-3-haiku-20240307");
    return models;
}

std::string ClaudeAI::default_model() const { return default_model_; }

bool ClaudeAI::is_configured() const { return !api_key_.empty(); }

CompletionResult ClaudeAI::complete(
    const std::string& prompt,
    const CompletionOptions& opts
) {
    std::vector<ConversationMessage> messages;
    messages.push_back(ConversationMessage::user(prompt));
    return chat(messages, opts);
}

CompletionResult ClaudeAI::chat(
    const std::vector<ConversationMessage>& messages,
    const CompletionOptions& opts
) {
    if (!initialized_) {
        return CompletionResult::fail("Claude AI not initialized");
    }
    
    if (messages.empty()) {
        return CompletionResult::fail("No messages provided");
    }
    
    LOG_DEBUG("[Claude] Starting chat request with %zu messages", messages.size());
    
    Json request = Json::object();
    
    std::string model = opts.model.empty() ? default_model_ : opts.model;
    request["model"] = model;
    LOG_DEBUG("[Claude] Using model: %s", model.c_str());
    
    int max_tokens = opts.max_tokens > 0 ? opts.max_tokens : 4096;
    request["max_tokens"] = static_cast<int64_t>(max_tokens);
    
    if (opts.temperature >= 0.0 && opts.temperature <= 1.0) {
        request["temperature"] = opts.temperature;
    }
    
    if (!opts.system_prompt.empty()) {
        request["system"] = opts.system_prompt;
        LOG_DEBUG("[Claude] System prompt (%zu chars): %.200s%s", 
                  opts.system_prompt.size(), 
                  opts.system_prompt.c_str(),
                  opts.system_prompt.size() > 200 ? "..." : "");
    }
    
    Json msgs = Json::array();
    LOG_DEBUG("[Claude] === ▶ IN  Messages being sent to AI ===");
    LOG_DEBUG("[Claude] ▶ IN  Model: %s, Max tokens: %d", model.c_str(), max_tokens);
    for (size_t i = 0; i < messages.size(); ++i) {
        const ConversationMessage& msg = messages[i];
        
        if (msg.role == MessageRole::SYSTEM) {
            if (opts.system_prompt.empty() && i == 0) {
                request["system"] = msg.content;
                LOG_DEBUG("[Claude]   [%zu] SYSTEM (%zu chars): %.200s%s", 
                          i, msg.content.size(), msg.content.c_str(),
                          msg.content.size() > 200 ? "..." : "");
            }
            continue;
        }
        
        Json m = Json::object();
        m["role"] = role_to_string(msg.role);
        m["content"] = msg.content;
        msgs.push_back(m);
        
        LOG_DEBUG("[Claude]   ▶ [%zu] %s (%zu chars): %.300s%s", 
                  i, role_to_string(msg.role).c_str(), 
                  msg.content.size(), msg.content.c_str(),
                  msg.content.size() > 300 ? "..." : "");
    }
    request["messages"] = msgs;
    LOG_DEBUG("[Claude] === ▶ IN  End of messages (%zu total) ===", msgs.size());
    
    if (opts.stream && opts.on_chunk) {
        request["stream"] = true;
    }
    
    std::string request_body = request.dump();
    LOG_DEBUG("[Claude] ▶ IN  Sending request to API (%zu bytes)", request_body.size());
    
    HttpClient http;
    std::map<std::string, std::string> headers;
    headers["x-api-key"] = api_key_;
    headers["anthropic-version"] = api_version_;
    headers["Content-Type"] = "application/json";
    
    HttpResponse response = http.post_json(api_url_, request_body, headers);
    
    if (response.status_code == 0) {
        LOG_ERROR("[Claude] HTTP request failed: %s", response.body.c_str());
        return CompletionResult::fail("HTTP request failed: " + response.body);
    }
    
    LOG_DEBUG("[Claude] ◀ OUT Received response [HTTP %d] (%zu bytes)", 
              response.status_code, response.body.size());
    
    Json resp = response.json();
    
    if (response.status_code != 200) {
        std::string error_msg = "API error";
        if (resp.is_object()) {
            if (resp.contains("error") && resp["error"].is_object()) {
                Json error = resp["error"];
                std::string msg = error.value("message", std::string(""));
                std::string type = error.value("type", std::string(""));
                if (!msg.empty()) {
                    error_msg = type.empty() ? msg : (type + ": " + msg);
                }
            }
        }
        LOG_ERROR("[Claude] API error: %s (HTTP %d)", error_msg.c_str(), response.status_code);
        return CompletionResult::fail(error_msg + " (HTTP " + 
                                     std::to_string(response.status_code) + ")");
    }
    
    CompletionResult result;
    result.success = true;
    result.model = resp.value("model", std::string(""));
    result.stop_reason = resp.value("stop_reason", std::string(""));
    
    Json content = resp["content"];
    if (content.is_array()) {
        std::ostringstream text;
        for (auto it = content.begin(); it != content.end(); ++it) {
            const Json& block = *it;
            std::string block_type = block.value("type", std::string(""));
            if (block_type == "text") {
                text << block.value("text", std::string(""));
            }
        }
        result.content = text.str();
    }
    
    Json usage = resp["usage"];
    if (usage.is_object()) {
        result.usage.input_tokens = usage.value("input_tokens", 0);
        result.usage.output_tokens = usage.value("output_tokens", 0);
        result.usage.total_tokens = result.usage.input_tokens + result.usage.output_tokens;
    }
    
    LOG_DEBUG("[Claude] === ◀ OUT AI Response ===");
    LOG_DEBUG("[Claude] ◀ OUT Model: %s, Stop reason: %s", result.model.c_str(), result.stop_reason.c_str());
    LOG_DEBUG("[Claude] ◀ OUT Tokens - Input: %d, Output: %d, Total: %d",
              result.usage.input_tokens, result.usage.output_tokens, result.usage.total_tokens);
    LOG_DEBUG("[Claude] ◀ OUT Content (%zu chars): %.500s%s", 
              result.content.size(), result.content.c_str(),
              result.content.size() > 500 ? "..." : "");
    LOG_DEBUG("[Claude] === ◀ OUT End AI Response ===");
    
    return result;
}

std::string ClaudeAI::ask(const std::string& question, const std::string& system) {
    CompletionOptions opts;
    if (!system.empty()) {
        opts.system_prompt = system;
    }
    CompletionResult result = complete(question, opts);
    if (result.success) {
        return result.content;
    }
    return "Error: " + result.error;
}

std::string ClaudeAI::reply(
    std::vector<ConversationMessage>& history,
    const std::string& user_message,
    const std::string& system
) {
    history.push_back(ConversationMessage::user(user_message));
    
    CompletionOptions opts;
    if (!system.empty()) {
        opts.system_prompt = system;
    }
    
    CompletionResult result = chat(history, opts);
    
    if (result.success) {
        history.push_back(ConversationMessage::assistant(result.content));
        return result.content;
    }
    
    history.pop_back();
    return "Error: " + result.error;
}

} // namespace opencrank

// Export plugin for dynamic loading
OPENCRANK_DECLARE_PLUGIN(opencrank::ClaudeAI, "claude", "1.0.0", 
                        "Anthropic Claude AI provider", "ai")
