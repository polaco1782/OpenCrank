#include <opencrank/plugins/llamacpp/llamacpp.hpp>
#include <opencrank/core/loader.hpp>
#include <sstream>

namespace opencrank {

LlamaCppAI::LlamaCppAI()
    : server_url_("http://localhost:8080")
    , api_key_()
    , default_model_("local-model")
    , max_context_chars_(16000)  // Conservative default: ~4K tokens * 4 chars/token
    , initialized_(false)
{}

const char* LlamaCppAI::name() const { return "Llama.cpp AI"; }
const char* LlamaCppAI::version() const { return "1.0.0"; }
const char* LlamaCppAI::description() const { 
    return "Llama.cpp server AI provider using OpenAI-compatible API"; 
}

bool LlamaCppAI::init(const Config& cfg) {
    server_url_ = cfg.get_string("llamacpp.url", "http://localhost:8080");
    api_key_ = cfg.get_string("llamacpp.api_key", "");
    
    std::string model = cfg.get_string("llamacpp.model", "");
    if (!model.empty()) {
        default_model_ = model;
    }
    
    // Context size configuration (in tokens — we multiply by 4 for char estimate)
    int context_tokens = static_cast<int>(cfg.get_int("llamacpp.context_size", 4096));
    max_context_chars_ = static_cast<size_t>(context_tokens) * 4;
    
    // Remove trailing slash from URL
    while (!server_url_.empty() && server_url_[server_url_.length() - 1] == '/') {
        server_url_ = server_url_.substr(0, server_url_.length() - 1);
    }
    
    LOG_INFO("Llama.cpp AI initialized with server: %s, model: %s, context: %d tokens (~%zu chars)", 
             server_url_.c_str(), default_model_.c_str(), context_tokens, max_context_chars_);
    initialized_ = true;
    return true;
}

void LlamaCppAI::shutdown() {
    initialized_ = false;
}

bool LlamaCppAI::is_initialized() const { return initialized_; }

std::string LlamaCppAI::provider_id() const { return "llamacpp"; }

std::vector<std::string> LlamaCppAI::available_models() const {
    std::vector<std::string> models;
    models.push_back(default_model_);
    return models;
}

std::string LlamaCppAI::default_model() const { return default_model_; }

bool LlamaCppAI::is_configured() const { return initialized_; }

CompletionResult LlamaCppAI::complete(
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

CompletionResult LlamaCppAI::chat(
    const std::vector<ConversationMessage>& messages,
    const CompletionOptions& opts
) {
    if (!initialized_) {
        return CompletionResult::fail("Llama.cpp AI not initialized");
    }
    
    if (messages.empty()) {
        return CompletionResult::fail("No messages provided");
    }
    
    LOG_DEBUG("[LlamaCpp] Starting chat request with %zu messages", messages.size());
    
    // Proactively trim messages to fit within context budget
    std::vector<ConversationMessage> trimmed_messages = trim_messages_to_fit(messages, opts.system_prompt);
    if (trimmed_messages.size() < messages.size()) {
        LOG_INFO("[LlamaCpp] Trimmed conversation from %zu to %zu messages to fit context window",
                 messages.size(), trimmed_messages.size());
    }
    
    // Build OpenAI-compatible request
    Json request = Json::object();
    
    std::string model = opts.model.empty() ? default_model_ : opts.model;
    request["model"] = model;
    LOG_DEBUG("[LlamaCpp] Using model: %s", model.c_str());
    
    // Add system prompt if provided
    if (!opts.system_prompt.empty()) {
        LOG_DEBUG("[LlamaCpp] === System Prompt ===");
        LOG_DEBUG("[LlamaCpp] System prompt (%zu chars): %.500s%s", 
                  opts.system_prompt.size(), 
                  opts.system_prompt.c_str(),
                  opts.system_prompt.size() > 500 ? "..." : "");
        LOG_DEBUG("[LlamaCpp] === End System Prompt ===");
    }
    
    // Convert messages to OpenAI format
    Json msgs = Json::array();
    
    // Prepend system message if system prompt is provided
    if (!opts.system_prompt.empty()) {
        Json sys_msg = Json::object();
        sys_msg["role"] = "system";
        sys_msg["content"] = opts.system_prompt;
        msgs.push_back(sys_msg);
    }
    
    LOG_DEBUG("[LlamaCpp] === Messages being sent to AI ===");
    for (size_t i = 0; i < trimmed_messages.size(); ++i) {
        const ConversationMessage& msg = trimmed_messages[i];
        
        Json m = Json::object();
        m["role"] = role_to_string(msg.role);
        m["content"] = msg.content;
        msgs.push_back(m);
        
        LOG_DEBUG("[LlamaCpp]   [%zu] %s (%zu chars): %.300s%s", 
                  i, role_to_string(msg.role).c_str(), 
                  msg.content.size(), msg.content.c_str(),
                  msg.content.size() > 300 ? "..." : "");
    }
    request["messages"] = msgs;
    LOG_DEBUG("[LlamaCpp] === End of messages ===");
    
    // Set parameters
    if (opts.temperature >= 0.0) {
        request["temperature"] = opts.temperature;
    }
    
    if (opts.max_tokens > 0) {
        request["max_tokens"] = static_cast<int64_t>(opts.max_tokens);
    }
    
    if (opts.stream) {
        request["stream"] = false;  // For now, disable streaming
    }
    
    std::string endpoint = server_url_ + "/v1/chat/completions";
    std::string request_body = request.dump();
    LOG_DEBUG("[LlamaCpp] Sending request to %s (%zu bytes)", endpoint.c_str(), request_body.size());
    
    // Prepare HTTP client
    HttpClient http;
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";
    
    if (!api_key_.empty()) {
        headers["Authorization"] = "Bearer " + api_key_;
    }
    
    // Make request to llama.cpp server
    HttpResponse response = http.post_json(endpoint, request_body, headers);
    
    if (response.status_code == 0) {
        LOG_ERROR("[LlamaCpp] HTTP request failed: %s", response.error.c_str());
        return CompletionResult::fail("HTTP request failed: " + response.error);
    }
    
    LOG_DEBUG("[LlamaCpp] Received response [HTTP %d] (%zu bytes)", 
              response.status_code, response.body.size());
    
    Json resp = response.json();
    
    if (response.status_code != 200) {
        std::string error_msg = "API error";
        if (resp.is_object()) {
            if (resp.contains("error") && resp["error"].is_object()) {
                std::string msg = resp["error"].value("message", std::string(""));
                if (!msg.empty()) {
                    error_msg = msg;
                }
            }
        }
        LOG_ERROR("[LlamaCpp] API error: %s (HTTP %d)", error_msg.c_str(), response.status_code);
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
            
            // Check for reasoning_content (some models put thinking here)
            std::string reasoning;
            if (message.contains("reasoning_content")) {
                reasoning = message.value("reasoning_content", std::string(""));
                LOG_DEBUG("[LlamaCpp] Found reasoning_content (%zu chars)", reasoning.size());
            }
            
            // Check for native OpenAI-style tool_calls in the response
            // This is the standard format: message.tool_calls = [{id, type, function: {name, arguments}}]
            if (message.contains("tool_calls") && message["tool_calls"].is_array() && 
                !message["tool_calls"].empty()) {
                
                const Json& tool_calls = message["tool_calls"];
                LOG_INFO("[LlamaCpp] Found %zu native tool_call(s) in response", tool_calls.size());
                
                std::ostringstream reconstructed;
                
                // Include any text content or reasoning as preamble
                if (!result.content.empty()) {
                    reconstructed << result.content << "\n\n";
                } else if (!reasoning.empty()) {
                    // Don't include raw reasoning as it confuses the agent
                    LOG_DEBUG("[LlamaCpp] Skipping reasoning_content preamble for native tool calls");
                }
                
                for (size_t tc = 0; tc < tool_calls.size(); ++tc) {
                    const Json& tc_obj = tool_calls[tc];
                    
                    if (!tc_obj.contains("function") || !tc_obj["function"].is_object()) {
                        LOG_WARN("[LlamaCpp] tool_call[%zu] missing 'function' object, skipping", tc);
                        continue;
                    }
                    
                    const Json& func = tc_obj["function"];
                    std::string tool_name = func.value("name", std::string(""));
                    std::string arguments = func.value("arguments", std::string("{}"));
                    
                    if (tool_name.empty()) {
                        LOG_WARN("[LlamaCpp] tool_call[%zu] has empty function name, skipping", tc);
                        continue;
                    }
                    
                    LOG_INFO("[LlamaCpp] Native tool_call[%zu]: %s", tc, tool_name.c_str());
                    LOG_DEBUG("[LlamaCpp] Native tool_call[%zu] arguments: %s", tc, arguments.c_str());
                    
                    // Reconstruct as <tool_call> XML format for the agent to parse
                    if (tc > 0) {
                        reconstructed << "\n\n";
                    }
                    reconstructed << "<tool_call name=\"" << tool_name << "\">\n";
                    reconstructed << arguments << "\n";
                    reconstructed << "</tool_call>";
                }
                
                result.content = reconstructed.str();
                LOG_INFO("[LlamaCpp] Reconstructed %zu native tool call(s) into <tool_call> format",
                         tool_calls.size());
                LOG_DEBUG("[LlamaCpp] Reconstructed content: %.500s%s", result.content.c_str(),
                          result.content.size() > 500 ? "..." : "");
            }
            // Fallback: content is empty, no tool_calls, but we have reasoning_content
            else if (result.content.empty() && !reasoning.empty()) {
                LOG_DEBUG("[LlamaCpp] No native tool_calls, checking raw body for tool call patterns");
                
                // The model may be using a non-standard structured format
                // Check the raw response body for tool call indicators
                std::string raw_body = response.body;
                
                // Look for "to=" pattern which indicates a tool call
                size_t to_pos = raw_body.find(" to=");
                if (to_pos != std::string::npos) {
                    LOG_DEBUG("[LlamaCpp] Found 'to=' pattern, attempting to reconstruct tool call");
                    
                    // Extract tool name
                    size_t tool_start = to_pos + 4;
                    size_t tool_end = raw_body.find_first_of(" <\n\r\"", tool_start);
                    if (tool_end != std::string::npos) {
                        std::string tool_name = raw_body.substr(tool_start, tool_end - tool_start);
                        
                        // Look for JSON params
                        size_t json_start = raw_body.find('{', tool_end);
                        if (json_start != std::string::npos && json_start < tool_end + 100) {
                            int brace_count = 1;
                            size_t json_end = json_start + 1;
                            while (json_end < raw_body.size() && brace_count > 0) {
                                if (raw_body[json_end] == '{') brace_count++;
                                else if (raw_body[json_end] == '}') brace_count--;
                                json_end++;
                            }
                            
                            if (brace_count == 0) {
                                std::string json_params = raw_body.substr(json_start, json_end - json_start);
                                
                                // Unescape the JSON (it's inside a JSON string in the response)
                                std::string unescaped;
                                unescaped.reserve(json_params.size());
                                for (size_t i = 0; i < json_params.size(); ++i) {
                                    if (json_params[i] == '\\' && i + 1 < json_params.size()) {
                                        char next = json_params[i + 1];
                                        if (next == '"' || next == '\\' || next == 'n' || next == 't' || next == 'r') {
                                            if (next == '"') {
                                                unescaped += '"';
                                                i++;
                                                continue;
                                            }
                                        }
                                    }
                                    unescaped += json_params[i];
                                }
                                
                                // Reconstruct as standard tool_call format
                                std::ostringstream reconstructed;
                                reconstructed << "<tool_call name=\"" << tool_name << "\">\n";
                                reconstructed << unescaped << "\n";
                                reconstructed << "</tool_call>";
                                
                                result.content = reconstructed.str();
                                LOG_INFO("[LlamaCpp] Reconstructed tool call from 'to=' pattern: %s", tool_name.c_str());
                                LOG_DEBUG("[LlamaCpp] Reconstructed content: %.300s", result.content.c_str());
                            }
                        }
                    }
                }
                
                // If we couldn't reconstruct, return the reasoning
                if (result.content.empty()) {
                    result.content = reasoning;
                }
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
    
    LOG_DEBUG("[LlamaCpp] === AI Response ===");
    LOG_DEBUG("[LlamaCpp] Model: %s, Stop reason: %s", result.model.c_str(), result.stop_reason.c_str());
    LOG_DEBUG("[LlamaCpp] Tokens - Input: %d, Output: %d, Total: %d",
              result.usage.input_tokens, result.usage.output_tokens, result.usage.total_tokens);
    LOG_DEBUG("[LlamaCpp] Response content (%zu chars): %.500s%s", 
              result.content.size(), result.content.c_str(),
              result.content.size() > 500 ? "..." : "");
    LOG_DEBUG("[LlamaCpp] === End AI Response ===");
    
    return result;
}

size_t LlamaCppAI::estimate_request_chars(
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

std::vector<ConversationMessage> LlamaCppAI::trim_messages_to_fit(
    const std::vector<ConversationMessage>& messages,
    const std::string& system_prompt) const
{
    size_t total_chars = estimate_request_chars(messages, system_prompt);
    
    // Leave room for the response (reserve ~25% of context for output)
    size_t budget = max_context_chars_ * 3 / 4;
    
    if (total_chars <= budget) {
        return messages; // Fits fine
    }
    
    LOG_WARN("[LlamaCpp] Request too large (%zu chars) for context budget (%zu chars), trimming",
             total_chars, budget);
    
    if (messages.empty()) {
        return messages;
    }
    
    // Strategy: keep the first message (usually the user's initial question context)
    // and the most recent messages, dropping middle ones.
    // Also truncate individual large tool_result messages.
    
    // First pass: truncate large individual messages in a copy
    std::vector<ConversationMessage> trimmed = messages;
    const size_t max_single_msg = budget / 4; // No single message should use more than 25% of budget
    
    for (size_t i = 0; i < trimmed.size(); ++i) {
        if (trimmed[i].content.size() > max_single_msg) {
            // Check if it's a tool result
            if (trimmed[i].content.find("<tool_result") != std::string::npos) {
                std::string truncated_content = trimmed[i].content.substr(0, max_single_msg);
                truncated_content += "\n... [content truncated to fit context window] ...";
                trimmed[i].content = truncated_content;
                LOG_DEBUG("[LlamaCpp] Truncated large tool_result message %zu from %zu to %zu chars",
                          i, messages[i].content.size(), trimmed[i].content.size());
            } else {
                // Generic truncation
                trimmed[i].content = trimmed[i].content.substr(0, max_single_msg) +
                    "\n... [truncated] ...";
            }
        }
    }
    
    // Re-estimate after individual truncation
    total_chars = estimate_request_chars(trimmed, system_prompt);
    if (total_chars <= budget) {
        return trimmed;
    }
    
    // Second pass: drop middle messages, keeping first and last
    std::vector<ConversationMessage> result;
    result.push_back(trimmed[0]);
    
    // Add context truncation note as assistant message to maintain alternation
    if (trimmed[0].role == MessageRole::USER) {
        result.push_back(ConversationMessage::assistant(
            "[Earlier conversation truncated to fit context window.]"
        ));
    }
    
    // Walk backwards from the end, adding messages until we hit the budget
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
    
    // Reverse tail and append
    for (size_t i = tail.size(); i > 0; --i) {
        // Ensure proper alternation
        if (!result.empty() && result.back().role == tail[i - 1].role) {
            continue; // Skip to avoid consecutive same-role
        }
        result.push_back(tail[i - 1]);
    }
    
    LOG_INFO("[LlamaCpp] Trimmed from %zu to %zu messages (est. %zu chars)",
             messages.size(), result.size(), estimate_request_chars(result, system_prompt));
    
    return result;
}

std::string LlamaCppAI::ask(const std::string& question, const std::string& system) {
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

std::string LlamaCppAI::reply(
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
OPENCRANK_DECLARE_PLUGIN(opencrank::LlamaCppAI, "llamacpp", "1.0.0", 
                        "Llama.cpp AI provider", "ai")
