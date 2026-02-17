#include <opencrank/plugins/openrouter/openrouter.hpp>
#include <opencrank/core/loader.hpp>
#include <opencrank/core/utils.hpp>
#include <sstream>

namespace opencrank {

OpenRouterAI::OpenRouterAI()
    : api_key_()
    , default_model_("openai/gpt-4o")
    , api_url_("https://openrouter.ai/api/v1")
    , initialized_(false)
{}

const char* OpenRouterAI::name() const { return "OpenRouter AI"; }
const char* OpenRouterAI::version() const { return "1.0.0"; }
const char* OpenRouterAI::description() const { 
    return "OpenRouter AI provider using OpenAI-compatible API"; 
}

bool OpenRouterAI::init(const Config& cfg) {
    api_key_ = cfg.get_string("openrouter.api_key", "");
    
    std::string model = cfg.get_string("openrouter.model", "");
    if (!model.empty()) {
        default_model_ = model;
    }
    
    std::string url = cfg.get_string("openrouter.api_url", "");
    if (!url.empty()) {
        api_url_ = url;
    }
    
    // Remove trailing slash from URL
    while (!api_url_.empty() && api_url_[api_url_.length() - 1] == '/') {
        api_url_ = api_url_.substr(0, api_url_.length() - 1);
    }
    
    if (api_key_.empty()) {
        LOG_WARN("OpenRouter AI: No API key configured (set openrouter.api_key in config.json)");
        initialized_ = false;
        return false;
    }

    // Context size configuration (in tokens — we multiply by 4 for char estimate)
    int context_tokens = static_cast<int>(cfg.get_int("openrouter.context_size", 16384));
    max_context_chars_ = static_cast<size_t>(context_tokens) * 4;
   
    // Configure the context manager for intelligent context window management
    ContextManagerConfig ctx_config;
    ctx_config.max_context_chars = max_context_chars_;
    ctx_config.reserve_for_response = max_context_chars_ / 4;  // Reserve 25% for response
    ctx_config.usage_threshold = 0.75;  // Trigger resume at 75%
    ctx_config.max_resume_chars = 3000;
    ctx_config.auto_save_memory = true;
    context_manager_.set_config(ctx_config);
    
    LOG_INFO("Context manager configured: max %zu chars, reserve %zu chars, threshold %.0f%%, auto-save %s",
             ctx_config.max_context_chars, ctx_config.reserve_for_response, 
             ctx_config.usage_threshold * 100.0, ctx_config.auto_save_memory ? "enabled" : "disabled");
    
    LOG_INFO("OpenRouter AI initialized with model: %s", default_model_.c_str());
    initialized_ = true;
    return true;
}

void OpenRouterAI::shutdown() {
    initialized_ = false;
}

bool OpenRouterAI::is_initialized() const { return initialized_; }

std::string OpenRouterAI::provider_id() const { return "openrouter"; }

std::vector<std::string> OpenRouterAI::available_models() const {
    std::vector<std::string> models;
    models.push_back("openai/gpt-4o");
    models.push_back("openai/gpt-4o-mini");
    models.push_back("anthropic/claude-sonnet-4");
    models.push_back("anthropic/claude-haiku-3.5");
    models.push_back("google/gemini-2.5-pro-preview");
    models.push_back("google/gemini-2.0-flash");
    models.push_back("meta-llama/llama-4-maverick");
    models.push_back("deepseek/deepseek-r1");
    models.push_back("mistralai/mistral-large");
    return models;
}

std::string OpenRouterAI::default_model() const { return default_model_; }

bool OpenRouterAI::is_configured() const { return !api_key_.empty(); }

CompletionResult OpenRouterAI::complete(
    const std::string& prompt,
    const CompletionOptions& opts
) {
    std::vector<ConversationMessage> messages;
    if (!opts.system_prompt.empty()) {
        messages.push_back(ConversationMessage::system(opts.system_prompt));
    }
    messages.push_back(ConversationMessage::user(prompt));
    return chat(messages, opts);
}

CompletionResult OpenRouterAI::chat(
    const std::vector<ConversationMessage>& messages,
    const CompletionOptions& opts
) {
    if (!initialized_) {
        return CompletionResult::fail("OpenRouter AI not initialized");
    }
    
    if (messages.empty()) {
        return CompletionResult::fail("No messages provided");
    }
    
    LOG_DEBUG("Starting chat request with %zu messages", messages.size());
    
    // Manage context window intelligently (resume-based strategy)
    // Skip if disabled for internal operations like resume generation
    std::vector<ConversationMessage> trimmed_messages;
    if (!opts.skip_context_management) {
        LOG_DEBUG("Checking context management for %zu messages", messages.size());
        trimmed_messages = manage_context(messages, opts.system_prompt);
        if (trimmed_messages.size() != messages.size()) {
            LOG_INFO(" Context managed: %zu -> %zu messages",
                     messages.size(), trimmed_messages.size());
        }
    } else {
        LOG_DEBUG("Skipping context management (skip_context_management=true)");
        trimmed_messages = messages;
    }
    
    // Build OpenAI-compatible request
    Json request = Json::object();
    
    std::string model = opts.model.empty() ? default_model_ : opts.model;
    request["model"] = model;
    LOG_DEBUG("Using model: %s", model.c_str());
       
    // Convert messages to OpenAI format
    Json msgs = Json::array();
    
    // Prepend system message if system prompt is provided
    if (!opts.system_prompt.empty()) {
        Json sys_msg = Json::object();
        sys_msg["role"] = "system";
        sys_msg["content"] = sanitize_utf8(opts.system_prompt);
        msgs.push_back(sys_msg);
    }
    
    LOG_DEBUG("=== ▶ IN  Messages being sent to AI ===");
    LOG_DEBUG("▶ IN  Model: %s", model.c_str());
    
    for (size_t i = 0; i < trimmed_messages.size(); ++i) {
        const ConversationMessage& msg = trimmed_messages[i];
        
        // Skip system messages if we already added the system prompt
        if (msg.role == MessageRole::SYSTEM) {
            if (!opts.system_prompt.empty()) {
                continue;
            }
        }
        
        Json m = Json::object();
        m["role"] = role_to_string(msg.role);
        m["content"] = sanitize_utf8(msg.content);
        msgs.push_back(m);
        
        LOG_DEBUG("▶ [%zu] %s (%zu chars): %.300s%s", 
                  i, role_to_string(msg.role).c_str(), 
                  msg.content.size(), msg.content.c_str(),
                  msg.content.size() > 300 ? "..." : "");
    }
    request["messages"] = msgs;
    LOG_DEBUG("=== ▶ IN  End of messages (%zu total) ===", msgs.size());
    
    // Set parameters
    if (opts.temperature >= 0.0) {
        request["temperature"] = opts.temperature;
    }
    
    if (opts.max_tokens > 0) {
        request["max_tokens"] = static_cast<int64_t>(opts.max_tokens);
    }
    
    std::string endpoint = api_url_ + "/chat/completions";
    std::string request_body = request.dump(-1, ' ', false, Json::error_handler_t::replace);
    LOG_DEBUG("▶ IN  Sending request to %s (%zu bytes)", endpoint.c_str(), request_body.size());
    
    // Prepare HTTP client
    HttpClient http;
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";
    headers["Authorization"] = "Bearer " + api_key_;
    
    // Make request to OpenRouter API
    HttpResponse response = http.post_json(endpoint, request_body, headers);
    
    if (response.status_code == 0) {
        LOG_ERROR(" HTTP request failed: %s", response.error.c_str());
        return CompletionResult::fail("HTTP request failed: " + response.error);
    }
    
    LOG_DEBUG("◀ OUT Received response [HTTP %d] (%zu bytes)", 
              response.status_code, response.body.size());
    
    std::string sanitized_body = sanitize_utf8(response.body);
    Json resp;
    try {
        resp = Json::parse(sanitized_body);
    } catch (const std::exception& e) {
        LOG_ERROR(" Failed to parse JSON response: %s", e.what());
        return CompletionResult::fail("Invalid JSON response: " + std::string(e.what()));
    }
    
    if (response.status_code != 200) {
        std::string error_msg = "API error";
        if (resp.is_object()) {
            if (resp.contains("error") && resp["error"].is_object()) {
                std::string msg = resp["error"].value("message", std::string(""));
                // Handle code as either string or number
                std::string code_str;
                if (resp["error"].contains("code")) {
                    const Json& code_field = resp["error"]["code"];
                    if (code_field.is_string()) {
                        code_str = code_field.get<std::string>();
                    } else if (code_field.is_number()) {
                        code_str = std::to_string(code_field.get<int64_t>());
                    }
                }
                if (!msg.empty()) {
                    error_msg = code_str.empty() ? msg : (code_str + ": " + msg);
                }
            }
        }
        LOG_ERROR(" API error: %s (HTTP %d)", error_msg.c_str(), response.status_code);
        return CompletionResult::fail(error_msg + " (HTTP " +
                                     std::to_string(response.status_code) + ")");
    }
    
    // Parse OpenAI-compatible response
    CompletionResult result;
    result.success = true;
    result.model = resp.value("model", std::string(""));
    
    if (resp.contains("choices") && resp["choices"].is_array() && !resp["choices"].empty()) {
        const Json& first_choice = resp["choices"][0];
        
        if (first_choice.contains("message") && first_choice["message"].is_object()) {
            const Json& message = first_choice["message"];
            result.content = message.value("content", std::string(""));
            
            // Check for native OpenAI-style tool_calls in the response
            if (message.contains("tool_calls") && message["tool_calls"].is_array() && 
                !message["tool_calls"].empty()) {
                
                const Json& tool_calls = message["tool_calls"];
                LOG_INFO(" Found %zu native tool_call(s) in response", tool_calls.size());
                
                std::ostringstream reconstructed;
                
                if (!result.content.empty()) {
                    reconstructed << result.content << "\n\n";
                }
                
                for (size_t tc = 0; tc < tool_calls.size(); ++tc) {
                    const Json& tc_obj = tool_calls[tc];
                    
                    if (!tc_obj.contains("function") || !tc_obj["function"].is_object()) {
                        LOG_WARN(" tool_call[%zu] missing 'function' object, skipping", tc);
                        continue;
                    }
                    
                    const Json& func = tc_obj["function"];
                    std::string tool_name = func.value("name", std::string(""));
                    std::string arguments = func.value("arguments", std::string("{}"));
                    
                    if (tool_name.empty()) {
                        LOG_WARN(" tool_call[%zu] has empty function name, skipping", tc);
                        continue;
                    }
                    
                    LOG_INFO(" Native tool_call[%zu]: %s", tc, tool_name.c_str());
                    LOG_DEBUG("Native tool_call[%zu] arguments: %s", tc, arguments.c_str());
                    
                    if (tc > 0) {
                        reconstructed << "\n\n";
                    }
                    reconstructed << "{\"tool\": \"" << tool_name << "\", ";
                    reconstructed << "\"arguments\": " << arguments << "}";
                }
                
                result.content = reconstructed.str();
                LOG_INFO(" Reconstructed %zu native tool call(s) into JSON format",
                         tool_calls.size());
            }
        }
        
        result.stop_reason = first_choice.value("finish_reason", std::string(""));
    }
    
    if (resp.contains("usage") && resp["usage"].is_object()) {
        const Json& usage = resp["usage"];
        result.usage.input_tokens = usage.value("prompt_tokens", 0);
        result.usage.output_tokens = usage.value("completion_tokens", 0);
        result.usage.total_tokens = usage.value("total_tokens", 0);
    }
    
    LOG_DEBUG("=== ◀ OUT AI Response ===");
    LOG_DEBUG("◀ OUT Model: %s, Stop reason: %s", result.model.c_str(), result.stop_reason.c_str());
    LOG_DEBUG("◀ OUT Tokens - Input: %d, Output: %d, Total: %d",
              result.usage.input_tokens, result.usage.output_tokens, result.usage.total_tokens);
    LOG_DEBUG("◀ OUT Content (%zu chars): %.500s%s", 
              result.content.size(), result.content.c_str(),
              result.content.size() > 500 ? "..." : "");
    LOG_DEBUG("=== ◀ OUT End AI Response ===");
    
    return result;
}

std::string OpenRouterAI::ask(const std::string& question, const std::string& system) {
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

std::string OpenRouterAI::reply(
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

size_t OpenRouterAI::estimate_request_chars(
    const std::vector<ConversationMessage>& messages,
    const std::string& system_prompt) const
{
    size_t total = system_prompt.size();
    for (size_t i = 0; i < messages.size(); ++i) {
        total += messages[i].content.size();
        total += 20; // Role tags, formatting overhead per message
    }
    return total;
}

std::vector<ConversationMessage> OpenRouterAI::manage_context(
    const std::vector<ConversationMessage>& messages,
    const std::string& system_prompt)
{
    if (messages.empty()) {
        return messages;
    }
    
    // Debug: Log current context usage
    ContextUsage usage = context_manager_.estimate_usage(messages, system_prompt);
    LOG_INFO(" Context usage: %.1f%% (%zu/%zu chars, %zu messages)",
             usage.usage_ratio * 100.0, usage.total_chars, usage.budget_chars, messages.size());
    
    // Check if we need a resume cycle
    if (context_manager_.needs_resume(messages, system_prompt)) {
        ContextUsage resume_usage = context_manager_.estimate_usage(messages, system_prompt);
        LOG_WARN(" Context at %.0f%% capacity (%zu/%zu chars), initiating resume cycle",
                 resume_usage.usage_ratio * 100.0, resume_usage.total_chars, resume_usage.budget_chars);
        
        // Perform the resume cycle: generate summary, save memory, wipe, reload
        std::vector<ConversationMessage> history_copy = messages;
        bool ok = context_manager_.perform_resume_cycle(
            this, history_copy, system_prompt);
        
        if (ok) {
            LOG_INFO(" Resume cycle complete: %zu -> %zu messages",
                     messages.size(), history_copy.size());
            return history_copy;
        }
        
        LOG_WARN(" Resume cycle failed, falling back to simple truncation");
    }
    
    // If context fits or resume failed, check if simple truncation is needed
    size_t total_chars = estimate_request_chars(messages, system_prompt);
    size_t budget = max_context_chars_ * 3 / 4;
    
    if (total_chars <= budget) {
        return messages; // Fits fine
    }
    
    // Fallback: truncate large individual messages
    LOG_WARN(" Fallback truncation: %zu chars > %zu budget", total_chars, budget);
    
    std::vector<ConversationMessage> trimmed = messages;
    const size_t max_single_msg = budget / 4;
    
    for (size_t i = 0; i < trimmed.size(); ++i) {
        if (trimmed[i].content.size() > max_single_msg) {
            if (trimmed[i].content.find("[TOOL_RESULT") != std::string::npos) {
                trimmed[i].content = trimmed[i].content.substr(0, max_single_msg)
                    + "\n... [content truncated to fit context window] ...";
            } else {
                trimmed[i].content = trimmed[i].content.substr(0, max_single_msg)
                    + "\n... [truncated] ...";
            }
        }
    }
    
    total_chars = estimate_request_chars(trimmed, system_prompt);
    if (total_chars <= budget) {
        return trimmed;
    }
    
    // Last resort: drop middle messages
    std::vector<ConversationMessage> result;
    result.push_back(trimmed[0]);
    
    if (trimmed[0].role == MessageRole::USER) {
        result.push_back(ConversationMessage::assistant(
            "[Earlier conversation truncated to fit context window.]"
        ));
    }
    
    std::vector<ConversationMessage> tail;
    size_t used = estimate_request_chars(result, system_prompt);
    
    for (size_t i = trimmed.size(); i > 1; --i) {
        size_t idx = i - 1;
        size_t msg_cost = trimmed[idx].content.size() + 20;
        if (used + msg_cost <= budget) {
            tail.push_back(trimmed[idx]);
            used += msg_cost;
        } else {
            break;
        }
    }
    
    for (size_t i = tail.size(); i > 0; --i) {
        if (!result.empty() && result.back().role == tail[i - 1].role) {
            continue;
        }
        result.push_back(tail[i - 1]);
    }
    
    LOG_INFO(" Fallback trimmed from %zu to %zu messages",
             messages.size(), result.size());
    
    return result;
}

} // namespace opencrank

// Export plugin for dynamic loading
OPENCRANK_DECLARE_PLUGIN(opencrank::OpenRouterAI, "openrouter", "1.0.0", 
                        "OpenRouter AI provider", "ai")
