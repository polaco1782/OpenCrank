/*
 * opencrank C++11 - Agentic Loop
 * 
 * Implements an agentic loop that allows the AI to call tools and receive results.
 * Uses JSON format for tool invocations.
 * 
 * Tool Call Format:
 *   {"tool": "tool_name", "arguments": {"param1": "value1", "param2": "value2"}}
 * 
 * Tool Result Format (injected back into conversation as plain text):
 *   [TOOL_RESULT tool=tool_name success=true]
 *     ... result content ...
 *   [/TOOL_RESULT]
 */
#ifndef opencrank_CORE_AGENT_HPP
#define opencrank_CORE_AGENT_HPP

#include "json.hpp"
#include "logger.hpp"
#include "content_chunker.hpp"
#include <string>
#include <vector>
#include <map>
#include <functional>

namespace opencrank {

// Forward declarations
class AIPlugin;
struct CompletionOptions;
struct ConversationMessage;

// ============================================================================
// Tool Definition
// ============================================================================

// Schema for a tool parameter
struct ToolParamSchema {
    std::string name;
    std::string type;       // "string", "number", "boolean", "array", "object"
    std::string description;
    bool required;
    std::string default_value;
    
    ToolParamSchema() : required(false) {}
    ToolParamSchema(const std::string& n, const std::string& t, const std::string& d, bool r = false)
        : name(n), type(t), description(d), required(r) {}
};

// Tool execution result
struct AgentToolResult {
    bool success;
    std::string output;     // Text output to show AI
    std::string error;      // Error message if failed
    bool should_continue;   // Whether the agent should continue (default true)
    
    AgentToolResult() : success(false), should_continue(true) {}
    
    static AgentToolResult ok(const std::string& output) {
        AgentToolResult r;
        r.success = true;
        r.output = output;
        return r;
    }
    
    static AgentToolResult fail(const std::string& err) {
        AgentToolResult r;
        r.success = false;
        r.error = err;
        return r;
    }
    
    static AgentToolResult stop(const std::string& output) {
        AgentToolResult r;
        r.success = true;
        r.output = output;
        r.should_continue = false;
        return r;
    }
};

// Tool execution function type
typedef std::function<AgentToolResult(const Json& params)> ToolExecutor;

// Tool definition
struct AgentTool {
    std::string name;
    std::string description;
    std::vector<ToolParamSchema> params;
    ToolExecutor execute;
    
    AgentTool() {}
    AgentTool(const std::string& n, const std::string& d, ToolExecutor e)
        : name(n), description(d), execute(e) {}
};

// ============================================================================
// Parsed Tool Call
// ============================================================================

struct ParsedToolCall {
    std::string tool_name;
    Json params;
    std::string raw_content;  // Raw JSON content of the tool call
    size_t start_pos;       // Position in original text
    size_t end_pos;         // End position in original text
    bool valid;
    std::string parse_error;
    
    ParsedToolCall() : start_pos(0), end_pos(0), valid(false) {}
};

// ============================================================================
// Agent Loop Configuration
// ============================================================================

struct AgentConfig {
    int max_iterations;             // Maximum tool call iterations (default: 15)
    int max_consecutive_errors;     // Stop after this many consecutive errors (default: 5)
    bool echo_tool_calls;           // Include tool calls in response (default: false)
    bool verbose_results;           // Include full tool results in response (default: false)
    size_t max_tool_result_size;    // Max chars before chunking (default: 15000)
    bool auto_chunk_large_results;  // Automatically chunk large tool results (default: true)
    size_t chunk_size;              // Chunk size in chars for large content (0 = auto from context_size)
    size_t context_size;            // Context size in tokens from the AI model (0 = use defaults)
    
    AgentConfig() 
        : max_iterations(30)
        , max_consecutive_errors(5)
        , echo_tool_calls(false)
        , verbose_results(false)
        , max_tool_result_size(15000)
        , auto_chunk_large_results(true)
        , chunk_size(0)
        , context_size(0) {}
    
    // Get effective chunk size: if chunk_size is set use it,
    // otherwise derive from context_size (10% of context in chars),
    // otherwise fall back to 8000
    size_t effective_chunk_size() const {
        if (chunk_size > 0) return chunk_size;
        if (context_size > 0) {
            // Use ~10% of context window (tokens * 4 chars/token * 0.10)
            size_t derived = (context_size * 4) / 10;
            return derived > 2000 ? derived : 2000;  // minimum 2000 chars
        }
        return 8000;  // fallback default
    }
};

// ============================================================================
// Agent Loop Result
// ============================================================================

struct AgentResult {
    bool success;
    std::string final_response;     // Final AI response after all tool calls
    std::string error;              // Error if failed
    int iterations;                 // Number of iterations used
    int tool_calls_made;            // Total tool calls made
    std::vector<std::string> tools_used;  // Names of tools that were called
    bool paused;                    // True if paused at max iterations (awaiting /continue)
    std::string pause_message;      // Message to show user when paused
    
    AgentResult() : success(false), iterations(0), tool_calls_made(0), paused(false) {}
};

// ============================================================================
// Agent Class
// ============================================================================

class Agent {
public:
    Agent();
    ~Agent();
    
    // Register a tool
    void register_tool(const AgentTool& tool);
    void register_tool(const std::string& name, const std::string& desc, ToolExecutor executor);
    
    // Get registered tools
    const std::map<std::string, AgentTool>& tools() const { return tools_; }
    
    // Build tools section for system prompt
    std::string build_tools_prompt() const;
    
    // Parse tool calls from AI response
    std::vector<ParsedToolCall> parse_tool_calls(const std::string& response) const;
    
    // Execute a single tool call
    AgentToolResult execute_tool(const ParsedToolCall& call);
    
    // Format tool result for injection into conversation
    // If the result is too large, it will be chunked and a summary returned
    std::string format_tool_result(const std::string& tool_name, const AgentToolResult& result);
    
    // Extract text response (content outside tool calls)
    std::string extract_response_text(const std::string& response, 
                                       const std::vector<ParsedToolCall>& calls) const;
    
    // Run the full agentic loop
    AgentResult run(
        AIPlugin* ai,
        const std::string& user_message,
        std::vector<ConversationMessage>& history,
        const std::string& system_prompt,
        const AgentConfig& config = AgentConfig()
    );
    
    // Configuration
    void set_config(const AgentConfig& config) { config_ = config; }
    const AgentConfig& config() const { return config_; }
    
    // Access the content chunker (for tools to access stored content)
    ContentChunker& chunker() { return chunker_; }
    const ContentChunker& chunker() const { return chunker_; }

private:
    std::map<std::string, AgentTool> tools_;
    AgentConfig config_;
    ContentChunker chunker_;
    
    // Helper to check if response contains tool calls
    bool has_tool_calls(const std::string& response) const;
    
    // Check if an error message indicates token/context limit exceeded
    bool is_token_limit_error(const std::string& error) const;
    
    // Attempt to recover from token limit by truncating history
    bool try_truncate_history(std::vector<ConversationMessage>& history) const;
};

} // namespace opencrank

#endif // opencrank_CORE_AGENT_HPP
