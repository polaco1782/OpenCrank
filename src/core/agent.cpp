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
#include <set>
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
        
        // Try parsing directly first
        try {
            res.value = Json::parse(sanitized);
            res.ok = true;
            res.used = sanitized;
            res.error.clear();
            return res;
        } catch (const std::exception& e) {
            res.error = e.what();
        }
        
        // Advanced recovery: Try to fix common escaping issues
        // This handles cases like: {"command": "curl -H "Header: value""}
        // by escaping internal quotes within string values
        std::string fixed = sanitized;
        bool in_string = false;
        bool in_key = false;
        bool escape_next = false;
        size_t colon_pos = std::string::npos;
        
        for (size_t i = 0; i < fixed.size(); ++i) {
            if (escape_next) {
                escape_next = false;
                continue;
            }
            
            if (fixed[i] == '\\') {
                escape_next = true;
                continue;
            }
            
            if (fixed[i] == '"') {
                if (!in_string) {
                    // Starting a string
                    in_string = true;
                    // Check if this is a key (before colon) or value (after colon)
                    if (colon_pos == std::string::npos || i < colon_pos) {
                        in_key = true;
                    } else {
                        in_key = false;
                    }
                } else {
                    // Ending a string - but is this really the end or an unescaped quote?
                    // If we're in a value string and the next non-whitespace char is not , or },
                    // this might be an unescaped internal quote
                    if (!in_key && in_string) {
                        size_t next_char = i + 1;
                        while (next_char < fixed.size() && 
                               (fixed[next_char] == ' ' || fixed[next_char] == '\t' || 
                                fixed[next_char] == '\n' || fixed[next_char] == '\r')) {
                            next_char++;
                        }
                        
                        // If next significant char is not , or }, this quote should be escaped
                        if (next_char < fixed.size() && 
                            fixed[next_char] != ',' && 
                            fixed[next_char] != '}' && 
                            fixed[next_char] != ']') {
                            // Escape this quote
                            fixed.insert(i, "\\");
                            i++; // Skip the inserted backslash
                            continue; // Don't toggle in_string
                        }
                    }
                    in_string = false;
                    in_key = false;
                }
            } else if (fixed[i] == ':' && !in_string) {
                colon_pos = i;
            }
        }
        
        // Try parsing the fixed version
        try {
            res.value = Json::parse(fixed);
            res.ok = true;
            res.used = fixed;
            res.error.clear();
            LOG_DEBUG("[Agent] JSON recovery: auto-escaped internal quotes");
            return res;
        } catch (const std::exception& e) {
            // Recovery failed, keep original error
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
    oss << "You MUST use tools to complete tasks. Use this JSON format:\n\n";
    oss << "```json\n";
    oss << "{\n";
    oss << "  \"tool\": \"TOOLNAME\",\n";
    oss << "  \"arguments\": {\n";
    oss << "    \"param\": \"value\"\n";
    oss << "  }\n";
    oss << "}\n";
    oss << "```\n\n";
    
    oss << "**FORMAT Rules:**\n";
    oss << "1. Start IMMEDIATELY with the JSON tool call - NO explanatory text before it\n";
    oss << "2. You can call multiple tools by emitting multiple JSON objects\n";
    oss << "3. You can explain AFTER the tool call(s), never before\n\n";

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
    // Look for JSON tool call pattern: {"tool": "..."
    return response.find("\"tool\"") != std::string::npos;
}

std::vector<ParsedToolCall> Agent::parse_tool_calls(const std::string& response) const {
    std::vector<ParsedToolCall> calls;
    
    // Parse JSON tool calls: {"tool": "name", "arguments": {...}}
    // We scan for '{' characters and try to parse JSON objects that have a "tool" key.
    size_t pos = 0;
    while (pos < response.size()) {
        // Find next '{' that could start a tool call
        size_t brace_start = response.find('{', pos);
        if (brace_start == std::string::npos) break;
        
        // Quick check: does the area after this brace contain "tool"?
        // Look ahead a reasonable distance to avoid parsing random JSON
        size_t lookahead_end = std::min(brace_start + 200, response.size());
        std::string lookahead = response.substr(brace_start, lookahead_end - brace_start);
        if (lookahead.find("\"tool\"") == std::string::npos) {
            pos = brace_start + 1;
            continue;
        }
        
        // Find the matching closing brace
        int brace_count = 1;
        size_t scan = brace_start + 1;
        bool in_string = false;
        bool escape_next = false;
        while (scan < response.size() && brace_count > 0) {
            char c = response[scan];
            if (escape_next) {
                escape_next = false;
                scan++;
                continue;
            }
            if (c == '\\' && in_string) {
                escape_next = true;
                scan++;
                continue;
            }
            if (c == '"') {
                in_string = !in_string;
            } else if (!in_string) {
                if (c == '{') brace_count++;
                else if (c == '}') brace_count--;
            }
            scan++;
        }
        
        if (brace_count != 0) {
            // Unmatched braces, skip
            pos = brace_start + 1;
            continue;
        }
        
        std::string candidate = response.substr(brace_start, scan - brace_start);
        LOG_DEBUG("[Agent] Found candidate JSON at position %zu (length=%zu)", brace_start, candidate.size());
        
        // Try to parse the JSON
        JsonParseResult parsed = try_parse_json(candidate);
        if (!parsed.ok) {
            LOG_DEBUG("[Agent] Candidate JSON parse failed: %s", parsed.error.c_str());
            pos = brace_start + 1;
            continue;
        }
        
        // Check if this JSON has a "tool" key
        if (!parsed.value.is_object() || !parsed.value.contains("tool")) {
            pos = scan;
            continue;
        }
        
        std::string tool_name = parsed.value.value("tool", std::string(""));
        if (tool_name.empty()) {
            LOG_DEBUG("[Agent] JSON has 'tool' key but empty value, skipping");
            pos = scan;
            continue;
        }
        
        LOG_DEBUG("[Agent] Found JSON tool call: '%s'", tool_name.c_str());
        
        ParsedToolCall call;
        call.tool_name = tool_name;
        call.start_pos = brace_start;
        call.end_pos = scan;
        call.raw_content = candidate;
        
        // Extract arguments
        if (parsed.value.contains("arguments") && parsed.value["arguments"].is_object()) {
            call.params = parsed.value["arguments"];
            call.valid = true;
            LOG_DEBUG("[Agent] Parsed arguments for '%s': %s", 
                      tool_name.c_str(), call.params.dump().c_str());
        } else if (parsed.value.contains("arguments") && parsed.value["arguments"].is_string()) {
            // Sometimes models put a JSON string in arguments - try to parse it
            std::string args_str = parsed.value["arguments"].get<std::string>();
            JsonParseResult args_parsed = try_parse_json(args_str);
            if (args_parsed.ok && args_parsed.value.is_object()) {
                call.params = args_parsed.value;
                call.valid = true;
                LOG_DEBUG("[Agent] Parsed stringified arguments for '%s'", tool_name.c_str());
            } else {
                call.valid = false;
                call.parse_error = "Arguments field is a string but not valid JSON: " + args_str;
                LOG_WARN("[Agent] Failed to parse stringified arguments for '%s'", tool_name.c_str());
            }
        } else {
            // No arguments field - that's OK for tools with no params
            call.params = Json::object();
            call.valid = true;
            LOG_DEBUG("[Agent] No arguments for '%s', using empty params", tool_name.c_str());
        }
        
        calls.push_back(call);
        pos = scan;
        
        LOG_DEBUG("[Agent] Parsed tool call: %s (valid=%s)", 
                  tool_name.c_str(), call.valid ? "yes" : "no");
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
        hint += "\nExample: {\"tool\": \"shell\", \"arguments\": {\"command\": \"ls\"}}";
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
            // Provide helpful error message with escaping guidance
            std::ostringstream error_msg;
            error_msg << "Invalid tool call - JSON parsing failed.\n\n";
            error_msg << "Error: " << (recover_error.empty() ? call.parse_error : recover_error) << "\n\n";
            error_msg << "**Common issues:**\n";
            error_msg << "1. Unescaped quotes in strings - Use \\\" inside JSON strings\n";
            error_msg << "2. For curl commands, prefer single quotes on the outside:\n";
            error_msg << "   {\"command\": \"curl -H 'Header: value' 'https://url'\"}\n";
            error_msg << "3. Or properly escape all internal quotes:\n";
            error_msg << "   {\"command\": \"curl -H \\\"Header: value\\\" \\\"https://url\\\"\"}\n";
            error_msg << "4. For complex JSON payloads in curl -d, write to a file first:\n";
            error_msg << "   Use the 'write' tool to create a JSON file, then:\n";
            error_msg << "   {\"command\": \"curl -d @/tmp/payload.json https://url\"}\n\n";
            error_msg << "Raw content received:\n" << call.raw_content.substr(0, 500);
            if (call.raw_content.size() > 500) {
                error_msg << "... [truncated]";
            }
            return AgentToolResult::fail(error_msg.str());
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
    oss << "[TOOL_RESULT tool=" << tool_name 
        << " success=" << (result.success ? "true" : "false") << "]\n";
    
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
            
            // Include a preview (first portion) - keep small for context-limited models
            size_t preview_size = std::min(static_cast<size_t>(2000), result.output.size());
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
    
    oss << "\n[/TOOL_RESULT]";
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
        if (msg.role == MessageRole::USER && msg.content.find("[TOOL_RESULT") != std::string::npos) {
            // This is a tool result message - check if it's large
            if (msg.content.size() > 10000) {
                // Truncate to a summary
                size_t result_start = msg.content.find("[TOOL_RESULT");
                size_t result_end = msg.content.find("[/TOOL_RESULT]");
                if (result_end != std::string::npos) {
                    // Extract tool name
                    std::string tool_name = "unknown";
                    size_t name_start = msg.content.find("tool=", result_start);
                    if (name_start != std::string::npos) {
                        name_start += 5;
                        size_t name_end = msg.content.find_first_of(" ]", name_start);
                        if (name_end != std::string::npos) {
                            tool_name = msg.content.substr(name_start, name_end - name_start);
                        }
                    }
                    
                    // Create truncated version
                    std::ostringstream truncated;
                    truncated << "[TOOL_RESULT tool=" << tool_name << " success=true]\n";
                    truncated << "[Content truncated to fit context window - original was " 
                              << msg.content.size() << " characters]\n";
                    
                    // Keep first 2000 chars of content
                    size_t content_start = msg.content.find("]\n", result_start);
                    if (content_start != std::string::npos) {
                        content_start += 2;
                    } else {
                        content_start = result_start;
                    }
                    size_t content_len = result_end - content_start;
                    if (content_len > 2000) {
                        truncated << msg.content.substr(content_start, 2000);
                        truncated << "\n... [truncated] ...";
                    } else {
                        truncated << msg.content.substr(content_start, content_len);
                    }
                    truncated << "\n[/TOOL_RESULT]";
                    
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
        // Keep first message (original user message) and build a valid alternating sequence
        std::vector<ConversationMessage> new_history;
        new_history.push_back(history[0]);
        
        // If first message is user, add a synthetic assistant response as bridge
        if (history[0].role == MessageRole::USER) {
            new_history.push_back(ConversationMessage::assistant(
                "[Earlier conversation context was truncated to fit context window.]"
            ));
        }
        
        // Add last messages, ensuring proper alternation
        // Find the last user message and include from there
        size_t last_start = history.size() - 1;
        // Walk backwards to find a user message to start from (max 4 messages back)
        for (size_t back = 1; back <= 4 && back < history.size(); ++back) {
            size_t idx = history.size() - back;
            if (history[idx].role == MessageRole::USER) {
                last_start = idx;
                break;
            }
        }
        
        // Ensure we don't create consecutive same-role messages
        MessageRole last_role = new_history.back().role;
        for (size_t i = last_start; i < history.size(); ++i) {
            if (history[i].role == last_role) {
                // Skip to avoid consecutive same-role messages
                continue;
            }
            new_history.push_back(history[i]);
            last_role = history[i].role;
        }
        
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
    
    // Track initial history size so we can restore on failure
    size_t initial_history_size = history.size();
    
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
    
    // Track recent tool calls to detect duplicates across iterations
    // Key: "tool_name:params_json", Value: iteration when last executed
    std::map<std::string, int> recent_tool_calls;
    
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
                // Restore history to state before this agent run
                while (history.size() > initial_history_size) {
                    history.pop_back();
                }
                return result;
            }
            
            consecutive_errors++;
            if (consecutive_errors >= config.max_consecutive_errors) {
                result.error = "Too many consecutive AI errors: " + ai_result.error;
                // Restore history to state before this agent run
                while (history.size() > initial_history_size) {
                    history.pop_back();
                }
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
                "let me create",
                "let me write",
                "let me read",
                "let me check",
                "let me look",
                "let me search",
                "let me fetch",
                "let me browse",
                "let me run",
                "let me execute",
                "let me try",
                "let me make",
                "let me update",
                "let me modify",
                "let me delete",
                "let me remove",
                "let me add",
                "let me open",
                "let me download",
                "let me get",
                "let me see",
                "let me find",
                "let me use",
                "let me install",
                "i'll create",
                "i'll write",
                "i'll read",
                "i'll check",
                "i'll run",
                "i'll execute",
                "i'll fetch",
                "i'll browse",
                "i'll search",
                "i'll make",
                "i'll use",
                "i will create",
                "i will write",
                "i will run",
                "i need to create",
                "i need to write",
                "i need to read",
                "i need to check",
                "i need to run",
                "i need to fetch",
                "i need to browse",
                "i need to search",
                "i need to make",
                "now i'll",
                "now let me",
                "let's do that",
                "let's do it",
                "let's create",
                "let's check",
                "let's write",
                "let's run",
                "let's look",
                "let's fetch",
                "let's search",
                "let's make",
                "i should check",
                "i should write",
                "i should run", 
                "i should do",
                "i should use the",
                "i'll do that",
                "doing that now",
                "executing now",
                "running the command now",
                "let's execute it",
                "i'll emit the tool call",
                "i need to emit",
                "emitting tool call",
                "calling the tool",
                "i can handle using the",
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

                // stupid model won't act, lets kick it's arse
                std::string continuation_prompt = 
                    "You said you would take action but didn't use a tool. "
                    "Stop planning and ACT NOW. Emit the tool call immediately:\n\n"
                    "{\"tool\": \"TOOLNAME\", \"arguments\": {\"param\": \"value\"}}\n\n"
                    "Do NOT explain. Do NOT plan. Just emit the tool call.";
                
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
        
        // Deduplicate tool calls within this response
        std::set<std::string> seen_in_response;
        
        for (size_t i = 0; i < calls.size(); ++i) {
            const ParsedToolCall& call = calls[i];
            
            // Build a dedup key from tool name + params
            std::string dedup_key = call.tool_name + ":" + 
                (call.valid ? call.params.dump() : call.raw_content);
            
            // Skip exact duplicates within the same response
            if (seen_in_response.count(dedup_key)) {
                LOG_WARN("[Agent] Skipping duplicate tool call in same response: %s",
                         call.tool_name.c_str());
                results_oss << "[TOOL_RESULT tool=" << call.tool_name 
                            << " success=true]\n"
                            << "(Duplicate call skipped - same tool with same parameters "
                            << "was already called in this response)\n[/TOOL_RESULT]\n";
                continue;
            }
            seen_in_response.insert(dedup_key);
            
            // Warn about repeated calls across iterations (but still execute)
            std::map<std::string, int>::iterator prev = recent_tool_calls.find(dedup_key);
            if (prev != recent_tool_calls.end()) {
                int prev_iter = prev->second;
                LOG_WARN("[Agent] Tool '%s' called with same params as iteration %d (now %d)",
                         call.tool_name.c_str(), prev_iter, result.iterations);
                // If called in the immediately previous iteration with same params, skip
                if (prev_iter == result.iterations - 1) {
                    LOG_WARN("[Agent] Skipping repeated tool call from consecutive iteration: %s",
                             call.tool_name.c_str());
                    results_oss << "[TOOL_RESULT tool=" << call.tool_name 
                                << " success=true]\n"
                                << "(This exact tool call was already made in the previous iteration. "
                                << "The result has not changed. Please use the previous result "
                                << "or try a different approach.)\n[/TOOL_RESULT]\n";
                    continue;
                }
            }
            recent_tool_calls[dedup_key] = result.iterations;
            
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
    
    // Reached max iterations - pause instead of stopping
    LOG_WARN("[Agent] Reached max iterations (%d) - pausing for user confirmation", config.max_iterations);
    result.success = false;  // Not completed yet
    result.paused = true;
    
    std::ostringstream pause_msg;
    pause_msg << "⏸️ **Task paused after " << config.max_iterations << " iterations**\n\n";
    
    if (!accumulated_response.empty()) {
        pause_msg << "Progress so far:\n" << accumulated_response << "\n\n";
    }
    
    pause_msg << "The AI has made " << result.tool_calls_made << " tool calls ";
    pause_msg << "and needs more iterations to complete the task.\n\n";
    pause_msg << "**Options:**\n";
    pause_msg << "• `/continue` - Allow 15 more iterations\n";
    pause_msg << "• `/continue <N>` - Allow N more iterations\n";
    pause_msg << "• `/continue no-stop` - Remove iteration limit (use with caution)\n";
    pause_msg << "• `/cancel` - Stop the task\n";
    
    result.pause_message = pause_msg.str();
    result.final_response = result.pause_message;
    
    return result;
}

} // namespace opencrank
