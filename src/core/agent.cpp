/*
 * OpenCrank C++11 - Agentic Loop Implementation
 * 
 * Implements tool call parsing, execution, and the agentic conversation loop.
 * Built-in tools are in builtin_tools.cpp.
 */
#include <opencrank/core/agent.hpp>
#include <opencrank/ai/ai.hpp>
#include <opencrank/core/utils.hpp>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <fstream>

namespace opencrank {

// ============================================================================
// ContentChunker Implementation
// ============================================================================

ContentChunker::ContentChunker() : next_id_(1) {}

std::string ContentChunker::store(const std::string& content, const std::string& source, size_t chunk_size) {
    ChunkedContent cc;
    cc.id = "chunk_" + std::to_string(next_id_++);
    cc.full_content = content;
    cc.source = source;
    cc.chunk_size = chunk_size;
    cc.total_chunks = (content.size() + chunk_size - 1) / chunk_size;
    
    storage_[cc.id] = cc;
    
    LOG_DEBUG("[ContentChunker] Stored content '%s' from '%s': %zu bytes, %zu chunks",
              cc.id.c_str(), source.c_str(), content.size(), cc.total_chunks);
    
    return cc.id;
}

std::string ContentChunker::get_chunk(const std::string& id, size_t chunk_index) const {
    std::map<std::string, ChunkedContent>::const_iterator it = storage_.find(id);
    if (it == storage_.end()) {
        return "Error: Content ID '" + id + "' not found.";
    }
    
    const ChunkedContent& cc = it->second;
    if (chunk_index >= cc.total_chunks) {
        return "Error: Chunk index " + std::to_string(chunk_index) + 
               " out of range. Total chunks: " + std::to_string(cc.total_chunks);
    }
    
    size_t start = chunk_index * cc.chunk_size;
    size_t len = std::min(cc.chunk_size, cc.full_content.size() - start);
    
    std::ostringstream oss;
    oss << "[Chunk " << (chunk_index + 1) << "/" << cc.total_chunks 
        << " from " << cc.source << "]\n";
    oss << cc.full_content.substr(start, len);
    
    if (chunk_index + 1 < cc.total_chunks) {
        oss << "\n\n[Use content_chunk tool with id=\"" << id 
            << "\" and chunk=" << (chunk_index + 1) << " for next chunk]";
    } else {
        oss << "\n\n[End of content]";
    }
    
    return oss.str();
}

std::string ContentChunker::get_info(const std::string& id) const {
    std::map<std::string, ChunkedContent>::const_iterator it = storage_.find(id);
    if (it == storage_.end()) {
        return "Content ID '" + id + "' not found.";
    }
    
    const ChunkedContent& cc = it->second;
    std::ostringstream oss;
    oss << "Content ID: " << cc.id << "\n";
    oss << "Source: " << cc.source << "\n";
    oss << "Total size: " << cc.full_content.size() << " characters\n";
    oss << "Total chunks: " << cc.total_chunks << " (each ~" << cc.chunk_size << " chars)\n";
    
    return oss.str();
}

std::string ContentChunker::search(const std::string& id, const std::string& query, size_t context_chars) const {
    std::map<std::string, ChunkedContent>::const_iterator it = storage_.find(id);
    if (it == storage_.end()) {
        return "Content ID '" + id + "' not found.";
    }
    
    const ChunkedContent& cc = it->second;
    std::string content_lower = cc.full_content;
    std::string query_lower = query;
    
    // Convert to lowercase for case-insensitive search
    for (size_t i = 0; i < content_lower.size(); ++i) {
        content_lower[i] = tolower(content_lower[i]);
    }
    for (size_t i = 0; i < query_lower.size(); ++i) {
        query_lower[i] = tolower(query_lower[i]);
    }
    
    std::vector<size_t> matches;
    size_t pos = 0;
    while ((pos = content_lower.find(query_lower, pos)) != std::string::npos) {
        matches.push_back(pos);
        pos += query_lower.size();
        if (matches.size() >= 10) break; // Limit matches
    }
    
    if (matches.empty()) {
        return "No matches found for '" + query + "' in content.";
    }
    
    std::ostringstream oss;
    oss << "Found " << matches.size() << " match(es) for '" << query << "':\n\n";
    
    for (size_t i = 0; i < matches.size(); ++i) {
        size_t match_pos = matches[i];
        size_t start = (match_pos > context_chars) ? (match_pos - context_chars) : 0;
        size_t end = std::min(match_pos + query.size() + context_chars, cc.full_content.size());
        
        oss << "--- Match " << (i + 1) << " (at position " << match_pos << ") ---\n";
        if (start > 0) oss << "...";
        oss << cc.full_content.substr(start, end - start);
        if (end < cc.full_content.size()) oss << "...";
        oss << "\n\n";
    }
    
    return oss.str();
}

bool ContentChunker::has(const std::string& id) const {
    return storage_.find(id) != storage_.end();
}

void ContentChunker::clear() {
    storage_.clear();
    LOG_DEBUG("[ContentChunker] Cleared all stored content");
}

void ContentChunker::remove(const std::string& id) {
    storage_.erase(id);
}

size_t ContentChunker::get_total_chunks(const std::string& id) const {
    std::map<std::string, ChunkedContent>::const_iterator it = storage_.find(id);
    if (it == storage_.end()) {
        return 0;
    }
    return it->second.total_chunks;
}

// ============================================================================
// Helper Functions
// ============================================================================

namespace {

struct JsonParseResult {
    bool ok;
    std::string used;
    std::string error;
    Json value;
};

static std::string trim_whitespace(const std::string& input) {
    size_t start = input.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = input.find_last_not_of(" \t\n\r");
    return input.substr(start, end - start + 1);
}

static std::string remove_trailing_commas(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == ',') {
            size_t j = i + 1;
            while (j < input.size() && (input[j] == ' ' || input[j] == '\t' || input[j] == '\n' || input[j] == '\r')) {
                ++j;
            }
            if (j < input.size() && (input[j] == '}' || input[j] == ']')) {
                continue;  // skip trailing comma
            }
        }
        out.push_back(input[i]);
    }
    return out;
}

static JsonParseResult try_parse_json(const std::string& raw) {
    JsonParseResult res;
    res.ok = false;
    res.used = raw;

    try {
        res.value = Json::parse(raw);
        res.ok = true;
        return res;
    } catch (const std::exception& e) {
        res.error = e.what();
    }

    // Recovery: strip code fences and extract first JSON object
    std::string cleaned = raw;
    size_t fence_pos = std::string::npos;
    while ((fence_pos = cleaned.find("```")) != std::string::npos) {
        cleaned.erase(fence_pos, 3);
    }

    cleaned = trim_whitespace(cleaned);

    size_t first_brace = cleaned.find('{');
    size_t last_brace = cleaned.rfind('}');
    if (first_brace != std::string::npos && last_brace != std::string::npos && last_brace > first_brace) {
        std::string candidate = cleaned.substr(first_brace, last_brace - first_brace + 1);
        std::string sanitized = remove_trailing_commas(candidate);
        try {
            res.value = Json::parse(sanitized);
            res.ok = true;
            res.used = sanitized;
            res.error.clear();
            return res;
        } catch (const std::exception& e) {
            res.error = e.what();
            res.used = sanitized;
        }
    }

    return res;
}

static bool extract_kv_value(const std::string& content, const std::string& key, std::string& value_out) {
    const std::string quoted_key = "\"" + key + "\"";
    const std::string single_quoted_key = "'" + key + "'";

    std::vector<size_t> candidates;
    size_t pos = content.find(quoted_key);
    while (pos != std::string::npos) {
        candidates.push_back(pos + quoted_key.size());
        pos = content.find(quoted_key, pos + 1);
    }
    pos = content.find(single_quoted_key);
    while (pos != std::string::npos) {
        candidates.push_back(pos + single_quoted_key.size());
        pos = content.find(single_quoted_key, pos + 1);
    }

    pos = content.find(key);
    while (pos != std::string::npos) {
        bool left_ok = (pos == 0) || !(isalnum(content[pos - 1]) || content[pos - 1] == '_');
        bool right_ok = (pos + key.size() >= content.size()) ||
                        !(isalnum(content[pos + key.size()]) || content[pos + key.size()] == '_');
        if (left_ok && right_ok) {
            candidates.push_back(pos + key.size());
        }
        pos = content.find(key, pos + 1);
    }

    for (size_t i = 0; i < candidates.size(); ++i) {
        size_t cursor = candidates[i];
        while (cursor < content.size() && isspace(static_cast<unsigned char>(content[cursor]))) {
            ++cursor;
        }
        if (cursor >= content.size() || content[cursor] != ':') {
            continue;
        }
        ++cursor;
        while (cursor < content.size() && isspace(static_cast<unsigned char>(content[cursor]))) {
            ++cursor;
        }
        if (cursor >= content.size()) {
            continue;
        }

        char quote = content[cursor];
        if (quote == '\"' || quote == '\'') {
            size_t start = cursor + 1;
            size_t end = start;
            bool escaped = false;
            for (; end < content.size(); ++end) {
                char c = content[end];
                if (escaped) {
                    escaped = false;
                    continue;
                }
                if (c == '\\') {
                    escaped = true;
                    continue;
                }
                if (c == quote) {
                    break;
                }
            }
            if (end < content.size()) {
                value_out = content.substr(start, end - start);
                return true;
            }
        } else {
            size_t start = cursor;
            size_t end = start;
            while (end < content.size() && content[end] != ',' && content[end] != '}' && content[end] != '\n' && content[end] != '\r') {
                ++end;
            }
            value_out = trim_whitespace(content.substr(start, end - start));
            if (!value_out.empty()) {
                return true;
            }
        }
    }

    return false;
}

static bool recover_params_from_raw(const AgentTool& tool, const std::string& raw_content, Json& out_params, std::string& out_error) {
    std::string content = trim_whitespace(raw_content);
    if (content.empty() || content == "{}") {
        out_params = Json::object();
        return true;
    }

    JsonParseResult parsed = try_parse_json(content);
    if (parsed.ok) {
        out_params = parsed.value;
        return true;
    }

    Json recovered = Json::object();
    bool found_any = false;
    bool missing_required = false;

    for (size_t i = 0; i < tool.params.size(); ++i) {
        const ToolParamSchema& param = tool.params[i];
        std::string value;
        if (extract_kv_value(content, param.name, value)) {
            recovered[param.name] = value;
            found_any = true;
        } else if (param.required) {
            missing_required = true;
        }
    }

    if (found_any && !missing_required) {
        out_params = recovered;
        return true;
    }

    if (tool.params.size() == 1) {
        out_params = Json::object();
        out_params[tool.params[0].name] = content;
        return true;
    }

    out_error = parsed.error;
    return false;
}

} // namespace

// ============================================================================
// Agent Implementation
// ============================================================================

Agent::Agent() {}

Agent::~Agent() {}

void Agent::register_tool(const AgentTool& tool) {
    LOG_DEBUG("[Agent] Registering tool: %s", tool.name.c_str());
    tools_[tool.name] = tool;
}

void Agent::register_tool(const std::string& name, const std::string& desc, ToolExecutor executor) {
    AgentTool tool(name, desc, executor);
    register_tool(tool);
}

std::string Agent::build_tools_prompt() const {
    if (tools_.empty()) {
        return "";
    }
    
    std::ostringstream oss;
    oss << "## Available Tools\n\n";
    oss << "You MUST use tools to complete tasks. Use this exact format:\n\n";
    oss << "<tool_call name=\"TOOLNAME\">\n";
    oss << "{\"param\": \"value\"}\n";
    oss << "</tool_call>\n\n";

    oss << "### Large Content Handling\n";
    oss << "When a tool returns content too large to fit in context, it will be automatically chunked.\n";
    oss << "You'll see a message like 'Stored as chunk_N with X chunks'. To access this content:\n";
    oss << "- Use 'content_chunk' with id and chunk number (0-based) to retrieve specific chunks\n";
    oss << "- Use 'content_search' with id and query to search within the content\n";
    oss << "This allows you to work with large web pages, files, or command outputs.\n\n";

    oss << "### Web Fetching\n";
    oss << "When you need to fetch or read web content, use 'browser_extract_text' for readable text,\n";
    oss << "'browser_fetch' for raw HTML, and 'browser_get_links' for links.\n\n";

    oss << "### Tools:\n\n";
    
    for (std::map<std::string, AgentTool>::const_iterator it = tools_.begin();
         it != tools_.end(); ++it) {
        const AgentTool& tool = it->second;
        oss << "**" << tool.name << "**: " << tool.description << "\n";
        LOG_DEBUG("[Agent] Tool '%s' has %zu parameters", tool.name.c_str(), tool.params.size());
        
        if (!tool.params.empty()) {
            oss << "  Parameters:\n";
            for (size_t i = 0; i < tool.params.size(); ++i) {
                const ToolParamSchema& param = tool.params[i];
                oss << "  - `" << param.name << "` (" << param.type;
                if (param.required) oss << ", required";
                oss << "): " << param.description << "\n";

                LOG_DEBUG("[Agent] Tool '%s' parameter '%s': type=%s, required=%s", 
                          tool.name.c_str(), param.name.c_str(), param.type.c_str(), 
                          param.required ? "true" : "false");
            }
        }
        oss << "\n";
    }
    
    return oss.str();
}

bool Agent::has_tool_calls(const std::string& response) const {
    return response.find("<tool_call") != std::string::npos;
}

std::vector<ParsedToolCall> Agent::parse_tool_calls(const std::string& response) const {
    std::vector<ParsedToolCall> calls;
    
    // First try standard <tool_call> format
    size_t pos = 0;
    while (pos < response.size()) {
        // Find <tool_call
        size_t start = response.find("<tool_call", pos);
        if (start == std::string::npos) break;
        
        LOG_DEBUG("[Agent] Found <tool_call at position %zu", start);
        
        // Find the name attribute
        size_t name_start = response.find("name=\"", start);
        if (name_start == std::string::npos || name_start > start + 50) {
            // Invalid format, skip
            LOG_DEBUG("[Agent] No name attribute found within 50 chars of <tool_call, skipping");
            pos = start + 10;
            continue;
        }
        name_start += 6;  // Skip past name="
        
        size_t name_end = response.find("\"", name_start);
        if (name_end == std::string::npos) {
            LOG_DEBUG("[Agent] No closing quote for name attribute, skipping");
            pos = start + 10;
            continue;
        }
        
        std::string tool_name = response.substr(name_start, name_end - name_start);
        LOG_DEBUG("[Agent] Extracted tool name: '%s'", tool_name.c_str());
        
        // Find the end of opening tag
        size_t tag_end = response.find(">", name_end);
        if (tag_end == std::string::npos) {
            LOG_DEBUG("[Agent] No closing > for opening tag, skipping");
            pos = start + 10;
            continue;
        }
        tag_end++;  // Move past >
        LOG_DEBUG("[Agent] Opening tag ends at position %zu", tag_end);
        
        // Find </tool_call>
        size_t close_tag = response.find("</tool_call>", tag_end);
        std::string content;
        if (close_tag == std::string::npos) {
            // Fallback: try to recover a JSON object even if closing tag is missing
            LOG_DEBUG("[Agent] No closing </tool_call> found, attempting recovery");
            size_t json_start = response.find('{', tag_end);
            if (json_start != std::string::npos) {
                int brace_count = 1;
                size_t json_end = json_start + 1;
                while (json_end < response.size() && brace_count > 0) {
                    if (response[json_end] == '{') brace_count++;
                    else if (response[json_end] == '}') brace_count--;
                    json_end++;
                }
                if (brace_count == 0) {
                    close_tag = json_end;  // pretend content ends at JSON end
                    content = response.substr(json_start, json_end - json_start);
                    LOG_DEBUG("[Agent] Recovered tool_call content without closing tag");
                }
            }

            if (close_tag == std::string::npos) {
                LOG_DEBUG("[Agent] Recovery failed for missing </tool_call>, skipping");
                pos = tag_end;
                continue;
            }
        } else {
            LOG_DEBUG("[Agent] Found </tool_call> at position %zu", close_tag);
            // Extract the content between tags
            content = response.substr(tag_end, close_tag - tag_end);
        }
        
        LOG_DEBUG("[Agent] Raw content between tags for '%s' (length=%zu): '%s'", 
                  tool_name.c_str(), content.size(), content.c_str());
        
        content = trim_whitespace(content);
        
        LOG_DEBUG("[Agent] Trimmed content for '%s' (length=%zu): '%s'", 
                  tool_name.c_str(), content.size(), content.c_str());
        
        ParsedToolCall call;
        call.tool_name = tool_name;
        call.start_pos = start;
        if (response.compare(close_tag, 12, "</tool_call>") == 0) {
            call.end_pos = close_tag + 12;  // Length of </tool_call>
        } else {
            call.end_pos = close_tag;
        }
        call.raw_content = content;
        
        // Parse JSON parameters
        if (content.empty() || content == "{}") {
            call.params = Json::object();
            call.valid = true;
            LOG_DEBUG("[Agent] Parsed empty JSON params for '%s'", tool_name.c_str());
        } else {
            LOG_DEBUG("[Agent] Parsing JSON for '%s': %s", tool_name.c_str(), content.c_str());
            JsonParseResult parsed = try_parse_json(content);
            if (parsed.ok) {
                call.params = parsed.value;
                call.valid = true;
                if (parsed.used != content) {
                    LOG_DEBUG("[Agent] Recovered JSON for '%s' using extracted object: %s", 
                              tool_name.c_str(), parsed.used.c_str());
                }
                LOG_DEBUG("[Agent] Successfully parsed JSON for '%s'", tool_name.c_str());
            } else {
                call.valid = false;
                call.parse_error = std::string("JSON parse error: ") + parsed.error;
                LOG_WARN("[Agent] Failed to parse tool call params for '%s': %s", 
                         tool_name.c_str(), parsed.error.c_str());
                LOG_DEBUG("[Agent] Failed JSON content was: %s", content.c_str());
            }
        }
        
        calls.push_back(call);
        pos = call.end_pos;
        
        LOG_DEBUG("[Agent] Parsed tool call: %s (valid=%s)", 
                  tool_name.c_str(), call.valid ? "yes" : "no");
    }
    
    // If no standard tool calls found, try alternative format: <|channel|>commentary to=toolname
    if (calls.empty()) {
        LOG_DEBUG("[Agent] No standard tool calls found, trying alternative format...");
        pos = 0;
        while (pos < response.size()) {
            // Find "to=" which indicates a tool call in alternative format
            size_t to_pos = response.find(" to=", pos);
            if (to_pos == std::string::npos) break;
            
            // Extract tool name after "to="
            size_t tool_start = to_pos + 4;
            size_t tool_end = tool_start;
            while (tool_end < response.size() && 
                   response[tool_end] != ' ' && response[tool_end] != '<' && 
                   response[tool_end] != '\n' && response[tool_end] != '\r') {
                tool_end++;
            }
            
            std::string tool_name = response.substr(tool_start, tool_end - tool_start);
            
            // Look for JSON params after this - find next '{'
            size_t json_start = response.find('{', tool_end);
            if (json_start == std::string::npos || json_start > tool_end + 100) {
                pos = tool_end;
                continue;
            }
            
            // Find matching '}'
            int brace_count = 1;
            size_t json_end = json_start + 1;
            while (json_end < response.size() && brace_count > 0) {
                if (response[json_end] == '{') brace_count++;
                else if (response[json_end] == '}') brace_count--;
                json_end++;
            }
            
            if (brace_count == 0) {
                std::string json_str = response.substr(json_start, json_end - json_start);
                
                ParsedToolCall call;
                call.tool_name = tool_name;
                call.start_pos = to_pos;
                call.end_pos = json_end;
                call.raw_content = json_str;
                
                LOG_DEBUG("[Agent] Parsing alternative format JSON for '%s': %s", tool_name.c_str(), json_str.c_str());
                JsonParseResult parsed = try_parse_json(json_str);
                if (parsed.ok) {
                    call.params = parsed.value;
                    call.valid = true;
                    calls.push_back(call);
                    if (parsed.used != json_str) {
                        LOG_DEBUG("[Agent] Recovered alternative format JSON for '%s' using extracted object: %s",
                                  tool_name.c_str(), parsed.used.c_str());
                    }
                    LOG_INFO("[Agent] Parsed alternative format tool call: %s", tool_name.c_str());
                } else {
                    LOG_WARN("[Agent] Failed to parse alternative format JSON: %s", parsed.error.c_str());
                    LOG_DEBUG("[Agent] Failed alternative format JSON was: %s", json_str.c_str());
                }
            }
            
            pos = json_end;
        }
    }
    
    return calls;
}

AgentToolResult Agent::execute_tool(const ParsedToolCall& call) {
    // Check for common mistakes
    if (call.tool_name == "tool_call") {
        std::string hint = "ERROR: Used 'tool_call' as name. Must use actual tool name.\n";
        hint += "Available tools: ";
        bool first = true;
        for (std::map<std::string, AgentTool>::const_iterator t = tools_.begin(); t != tools_.end(); ++t) {
            if (!first) hint += ", ";
            hint += t->first;
            first = false;
        }
        hint += "\nExample: <tool_call name=\"bash\">{\"command\":\"ls\"}</tool_call>";
        return AgentToolResult::fail(hint);
    }
    
    std::map<std::string, AgentTool>::iterator it = tools_.find(call.tool_name);
    if (it == tools_.end()) {
        std::string error = "Unknown tool: " + call.tool_name + "\nAvailable tools: ";
        for (std::map<std::string, AgentTool>::const_iterator t = tools_.begin(); t != tools_.end(); ++t) {
            if (t != tools_.begin()) error += ", ";
            error += t->first;
        }
        return AgentToolResult::fail(error);
    }

    ParsedToolCall effective_call = call;
    if (!effective_call.valid) {
        Json recovered;
        std::string recover_error;
        if (recover_params_from_raw(it->second, effective_call.raw_content, recovered, recover_error)) {
            effective_call.params = recovered;
            effective_call.valid = true;
            LOG_DEBUG("[Agent] Recovered tool params for '%s' from raw content", call.tool_name.c_str());
        } else {
            return AgentToolResult::fail("Invalid tool call: " + (recover_error.empty() ? call.parse_error : recover_error));
        }
    }
    
    LOG_INFO("[Agent] Executing tool: %s", call.tool_name.c_str());
    LOG_DEBUG("[Agent] Tool params: %s", effective_call.params.dump().c_str());
    
    try {
        AgentToolResult result = it->second.execute(effective_call.params);
        LOG_DEBUG("[Agent] Tool %s result: success=%s, output_len=%zu",
                  call.tool_name.c_str(), result.success ? "yes" : "no", 
                  result.output.size());
        return result;
    } catch (const std::exception& e) {
        LOG_ERROR("[Agent] Tool %s threw exception: %s", call.tool_name.c_str(), e.what());
        return AgentToolResult::fail(std::string("Tool exception: ") + e.what());
    }
}

std::string Agent::format_tool_result(const std::string& tool_name, const AgentToolResult& result) {
    std::ostringstream oss;
    oss << "<tool_result name=\"" << tool_name << "\" success=\"" 
        << (result.success ? "true" : "false") << "\">\n";
    
    if (result.success) {
        // Check if the result is too large and should be chunked
        if (config_.auto_chunk_large_results && result.output.size() > config_.max_tool_result_size) {
            // Store the large content in the chunker
            std::string chunk_id = chunker_.store(result.output, tool_name);
            size_t total_chunks = chunker_.get_total_chunks(chunk_id);
            
            LOG_INFO("[Agent] Large tool result (%zu bytes) chunked as '%s' (%zu chunks)",
                     result.output.size(), chunk_id.c_str(), total_chunks);
            
            // Return a summary with the first chunk
            oss << "Content too large (" << result.output.size() << " characters). "
                << "Stored as '" << chunk_id << "' with " << total_chunks << " chunks.\n\n";
            
            // Include a preview (first portion)
            size_t preview_size = std::min(config_.max_tool_result_size / 2, result.output.size());
            oss << "=== Preview (first " << preview_size << " characters) ===\n";
            oss << result.output.substr(0, preview_size);
            if (preview_size < result.output.size()) {
                oss << "\n... [content truncated] ...\n";
            }
            
            oss << "\n\n=== To access full content ===\n";
            oss << "Use 'content_chunk' tool with id=\"" << chunk_id << "\" and chunk=0 to get first chunk.\n";
            oss << "Use 'content_search' tool with id=\"" << chunk_id << "\" and query=\"your search\" to find specific content.\n";
            oss << "Total chunks available: " << total_chunks;
        } else {
            oss << result.output;
        }
    } else {
        oss << "Error: " << result.error;
    }
    
    oss << "\n</tool_result>";
    return oss.str();
}

std::string Agent::extract_response_text(const std::string& response, 
                                          const std::vector<ParsedToolCall>& calls) const {
    if (calls.empty()) {
        return response;
    }
    
    std::string result;
    size_t pos = 0;
    
    for (size_t i = 0; i < calls.size(); ++i) {
        // Add text before this tool call
        if (calls[i].start_pos > pos) {
            result += response.substr(pos, calls[i].start_pos - pos);
        }
        pos = calls[i].end_pos;
    }
    
    // Add remaining text after last tool call
    if (pos < response.size()) {
        result += response.substr(pos);
    }
    
    // Trim excessive whitespace
    size_t start = result.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    
    size_t end = result.find_last_not_of(" \t\n\r");
    return result.substr(start, end - start + 1);
}

bool Agent::is_token_limit_error(const std::string& error) const {
    std::string error_lower = error;
    for (size_t i = 0; i < error_lower.size(); ++i) {
        error_lower[i] = tolower(error_lower[i]);
    }
    
    // Common patterns for token/context limit errors
    return (error_lower.find("exceeds") != std::string::npos && 
            (error_lower.find("context") != std::string::npos || 
             error_lower.find("token") != std::string::npos)) ||
           (error_lower.find("too long") != std::string::npos) ||
           (error_lower.find("context length") != std::string::npos) ||
           (error_lower.find("maximum context") != std::string::npos) ||
           (error_lower.find("token limit") != std::string::npos) ||
           (error_lower.find("context size") != std::string::npos);
}

bool Agent::try_truncate_history(std::vector<ConversationMessage>& history) const {
    if (history.size() < 3) {
        // Need at least: original user message + some context
        return false;
    }
    
    LOG_INFO("[Agent] Attempting to truncate history to fit context window (current: %zu messages)",
             history.size());
    
    // Strategy 1: Find and truncate large tool_result messages
    bool truncated_something = false;
    for (size_t i = 0; i < history.size(); ++i) {
        ConversationMessage& msg = history[i];
        if (msg.role == MessageRole::USER && msg.content.find("<tool_result") != std::string::npos) {
            // This is a tool result message - check if it's large
            if (msg.content.size() > 10000) {
                // Truncate to a summary
                size_t result_start = msg.content.find("<tool_result");
                size_t result_end = msg.content.find("</tool_result>");
                if (result_end != std::string::npos) {
                    // Extract tool name
                    std::string tool_name = "unknown";
                    size_t name_start = msg.content.find("name=\"", result_start);
                    if (name_start != std::string::npos) {
                        name_start += 6;
                        size_t name_end = msg.content.find("\"", name_start);
                        if (name_end != std::string::npos) {
                            tool_name = msg.content.substr(name_start, name_end - name_start);
                        }
                    }
                    
                    // Create truncated version
                    std::ostringstream truncated;
                    truncated << "<tool_result name=\"" << tool_name << "\" success=\"true\">\n";
                    truncated << "[Content truncated to fit context window - original was " 
                              << msg.content.size() << " characters]\n";
                    
                    // Keep first 2000 chars of content
                    size_t content_start = msg.content.find(">", result_start) + 1;
                    size_t content_len = result_end - content_start;
                    if (content_len > 2000) {
                        truncated << msg.content.substr(content_start, 2000);
                        truncated << "\n... [truncated] ...";
                    } else {
                        truncated << msg.content.substr(content_start, content_len);
                    }
                    truncated << "\n</tool_result>";
                    
                    LOG_DEBUG("[Agent] Truncated tool result for '%s' from %zu to %zu chars",
                              tool_name.c_str(), msg.content.size(), truncated.str().size());
                    
                    msg.content = truncated.str();
                    truncated_something = true;
                }
            }
        }
    }
    
    if (truncated_something) {
        LOG_INFO("[Agent] Truncated large tool results in history");
        return true;
    }
    
    // Strategy 2: Remove older tool call/result pairs (keep first and last few messages)
    if (history.size() > 6) {
        // Keep first 2 messages (likely original user message and first response)
        // Keep last 2 messages (most recent context)
        // Remove middle messages
        std::vector<ConversationMessage> new_history;
        new_history.push_back(history[0]);
        if (history.size() > 1) {
            new_history.push_back(history[1]);
        }
        
        // Add a note about removed context
        new_history.push_back(ConversationMessage::user(
            "[Note: Earlier conversation history was truncated to fit context window. "
            "Please continue based on available context.]"
        ));
        
        // Add last 2 messages
        if (history.size() >= 2) {
            new_history.push_back(history[history.size() - 2]);
        }
        new_history.push_back(history[history.size() - 1]);
        
        LOG_INFO("[Agent] Reduced history from %zu to %zu messages", 
                 history.size(), new_history.size());
        
        history = new_history;
        return true;
    }
    
    return false;
}

AgentResult Agent::run(
    AIPlugin* ai,
    const std::string& user_message,
    std::vector<ConversationMessage>& history,
    const std::string& system_prompt,
    const AgentConfig& config) {
    
    AgentResult result;
    result.iterations = 0;
    result.tool_calls_made = 0;
    
    if (!ai || !ai->is_configured()) {
        result.error = "AI not configured";
        return result;
    }
    
    LOG_INFO("[Agent] Starting agentic loop for message: %.50s%s", 
             user_message.c_str(), user_message.size() > 50 ? "..." : "");
    
    // Add user message to history
    history.push_back(ConversationMessage::user(user_message));
    
    // Build full system prompt with tools
    std::string full_system_prompt = system_prompt;
    std::string tools_prompt = build_tools_prompt();
    if (!tools_prompt.empty()) {
        full_system_prompt = tools_prompt + "\n\n" + system_prompt;
    }
    
    int consecutive_errors = 0;
    int token_limit_retries = 0;
    const int max_token_limit_retries = 2;
    std::string accumulated_response;
    
    // Agentic loop
    while (result.iterations < config.max_iterations) {
        result.iterations++;
        LOG_DEBUG("[Agent] === Iteration %d ===", result.iterations);
        
        // Call AI
        CompletionOptions opts;
        opts.system_prompt = full_system_prompt;
        opts.max_tokens = 4096;
        
        CompletionResult ai_result = ai->chat(history, opts);
        
        if (!ai_result.success) {
            LOG_ERROR("[Agent] AI call failed: %s", ai_result.error.c_str());
            
            // Check if this is a token limit error
            if (is_token_limit_error(ai_result.error)) {
                token_limit_retries++;
                LOG_WARN("[Agent] Token limit exceeded (attempt %d/%d), trying to recover...",
                         token_limit_retries, max_token_limit_retries);
                
                if (token_limit_retries <= max_token_limit_retries) {
                    // Try to truncate history and retry
                    if (try_truncate_history(history)) {
                        LOG_INFO("[Agent] History truncated, retrying...");
                        consecutive_errors = 0;  // Reset error count for this recovery attempt
                        continue;
                    } else {
                        LOG_WARN("[Agent] Could not truncate history further");
                    }
                }
                
                // If we've exhausted retries or can't truncate, fail gracefully
                result.error = "Context window exceeded and recovery failed. Try a simpler request or use smaller data.";
                history.pop_back();
                return result;
            }
            
            consecutive_errors++;
            if (consecutive_errors >= config.max_consecutive_errors) {
                result.error = "Too many consecutive AI errors: " + ai_result.error;
                // Remove user message on failure
                history.pop_back();
                return result;
            }
            continue;
        }
        
        consecutive_errors = 0;
        token_limit_retries = 0;  // Reset on successful call
        std::string response = ai_result.content;
        
        LOG_DEBUG("[Agent] AI response length: %zu", response.size());
        LOG_DEBUG("[Agent] AI response preview: %.300s%s", response.c_str(), 
                  response.size() > 300 ? "..." : "");
        
        // Parse tool calls
        std::vector<ParsedToolCall> calls = parse_tool_calls(response);
        
        if (calls.empty()) {
            // Check if the AI indicated intent to use a tool but didn't emit the call
            // This is common with smaller models that "think out loud"
            bool indicates_tool_intent = false;
            bool is_asking_question = false;
            std::string response_lower = response;
            std::transform(response_lower.begin(), response_lower.end(), response_lower.begin(), ::tolower);
            
            // Check if AI is asking a question (not intent to act)
            if (response_lower.find("?") != std::string::npos &&
                (response_lower.find("which") != std::string::npos ||
                 response_lower.find("what") != std::string::npos ||
                 response_lower.find("where") != std::string::npos ||
                 response_lower.find("could you") != std::string::npos ||
                 response_lower.find("would you") != std::string::npos ||
                 response_lower.find("do you want") != std::string::npos)) {
                is_asking_question = true;
                LOG_DEBUG("[Agent] AI is asking a question, not forcing tool call");
            }
            
            // Patterns that indicate the AI wants to use a tool NOW
            // Only trigger if it's a clear statement of intent to act immediately
            const char* intent_patterns[] = {
                "let's do that",
                "let's do it",
                "i'll do that",
                "doing that now",
                "executing now",
                "running the command now",
                "let's execute it",
                "i'll emit the tool call",
                "emitting tool call",
                "calling the tool",
                NULL
            };
            
            for (int i = 0; intent_patterns[i] != NULL && !is_asking_question; ++i) {
                if (response_lower.find(intent_patterns[i]) != std::string::npos) {
                    indicates_tool_intent = true;
                    LOG_DEBUG("[Agent] Detected tool intent pattern: '%s'", intent_patterns[i]);
                    break;
                }
            }
            
            // If AI indicated intent but no tool call, prompt it to actually emit the call
            if (indicates_tool_intent && !is_asking_question && result.iterations < config.max_iterations) {
                LOG_INFO("[Agent] AI indicated tool intent but didn't emit call, prompting to continue");
                
                // Add the AI's response to history
                history.push_back(ConversationMessage::assistant(response));
                
                // Add a prompt to actually emit the tool call
                std::string continuation_prompt = 
                    "You indicated you want to use a tool, but you didn't emit the actual tool call. "
                    "Please emit the tool call now using the exact format:\n\n"
                    "<tool_call name=\"TOOLNAME\">\n{\"param\": \"value\"}\n</tool_call>\n\n"
                    "Do not explain - just emit the tool call.";
                
                history.push_back(ConversationMessage::user(continuation_prompt));
                continue;  // Continue the loop to get the actual tool call
            }
            
            // No tool calls and no intent - we're done
            LOG_INFO("[Agent] No tool calls in response, loop complete after %d iterations", 
                     result.iterations);
            
            // Add final response to history
            history.push_back(ConversationMessage::assistant(response));
            
            result.success = true;
            result.final_response = response;
            return result;
        }
        
        // Execute tool calls and build results
        LOG_INFO("[Agent] Found %zu tool call(s) in response", calls.size());
        
        std::ostringstream results_oss;
        bool should_continue = true;
        
        for (size_t i = 0; i < calls.size(); ++i) {
            const ParsedToolCall& call = calls[i];
            result.tool_calls_made++;
            
            // Track tool usage
            if (std::find(result.tools_used.begin(), result.tools_used.end(), call.tool_name) 
                == result.tools_used.end()) {
                result.tools_used.push_back(call.tool_name);
            }
            
            AgentToolResult tool_result = execute_tool(call);
            
            if (!tool_result.should_continue) {
                should_continue = false;
            }
            
            results_oss << format_tool_result(call.tool_name, tool_result) << "\n";
        }
        
        // Extract text response (non-tool-call content)
        std::string text_response = extract_response_text(response, calls);
        
        // Add AI's response (with tool calls) to history
        history.push_back(ConversationMessage::assistant(response));
        
        // Add tool results as a user message (this continues the conversation)
        std::string tool_results = results_oss.str();
        LOG_DEBUG("[Agent] Tool results:\n%s", tool_results.c_str());
        
        history.push_back(ConversationMessage::user(tool_results));
        
        if (!should_continue) {
            LOG_INFO("[Agent] Tool requested stop, ending loop");
            result.success = true;
            result.final_response = text_response.empty() ? "Task completed." : text_response;
            return result;
        }
        
        // Accumulate non-tool response text
        if (!text_response.empty()) {
            if (!accumulated_response.empty()) {
                accumulated_response += "\n\n";
            }
            accumulated_response += text_response;
        }
    }
    
    // Reached max iterations
    LOG_WARN("[Agent] Reached max iterations (%d)", config.max_iterations);
    result.success = true;  // Partial success
    result.final_response = accumulated_response.empty() 
        ? "Reached maximum tool call iterations." 
        : accumulated_response + "\n\n(Reached maximum iterations)";
    
    return result;
}

} // namespace opencrank
