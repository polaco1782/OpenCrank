/*
 * opencrank C++11 - Built-in Agent Tools
 * 
 * Provides filesystem and shell tools for the agent:
 * - read: Read file contents
 * - write: Write content to files
 * - shell: Execute shell commands
 * - list_dir: List directory contents
 * - content_chunk: Retrieve chunks of large content
 * - content_search: Search within large content
 */
#ifndef opencrank_CORE_BUILTIN_TOOLS_HPP
#define opencrank_CORE_BUILTIN_TOOLS_HPP

#include "agent.hpp"
#include "tool.hpp"
#include "config.hpp"
#include <string>
#include <memory>

namespace opencrank {

// Forward declaration
class ContentChunker;

// ============================================================================
// Built-in Tools Provider
// ============================================================================

/**
 * BuiltinToolsProvider - Provides filesystem and shell tools
 * 
 * This class implements the ToolProvider interface to provide consistent
 * tool registration alongside plugin-based tools (browser, memory, etc.)
 */
class BuiltinToolsProvider : public ToolProvider {
public:
    BuiltinToolsProvider();
    virtual ~BuiltinToolsProvider();
    
    // Plugin interface
    const char* name() const override { return "builtin_tools"; }
    const char* description() const override { 
        return "Built-in filesystem and shell tools"; 
    }
    const char* version() const override { return "1.0.0"; }
    
    bool init(const Config& cfg) override;
    void shutdown() override;
    
    // ToolProvider interface
    const char* tool_id() const override { return "builtin"; }
    std::vector<std::string> actions() const override;
    ToolResult execute(const std::string& action, const Json& params) override;
    
    // Override to provide detailed tool descriptions
    std::vector<AgentTool> get_agent_tools() const override;
    
    // Set the content chunker (called by Application after agent is set up)
    void set_chunker(ContentChunker* chunker) { chunker_ = chunker; }
    
private:
    std::string workspace_dir_;
    int shell_timeout_;
    ContentChunker* chunker_;
    
    // Internal tool implementations
    AgentToolResult do_read(const Json& params) const;
    AgentToolResult do_write(const Json& params) const;
    AgentToolResult do_shell(const Json& params) const;
    AgentToolResult do_list_dir(const Json& params) const;
    AgentToolResult do_content_chunk(const Json& params) const;
    AgentToolResult do_content_search(const Json& params) const;
};

// ============================================================================
// Tool Factory Functions (Legacy - for backward compatibility)
// ============================================================================

namespace builtin_tools {

/**
 * Create a file reading tool.
 * 
 * @param workspace_dir Base directory for relative paths
 * @return Configured AgentTool
 */
AgentTool create_read_tool(const std::string& workspace_dir);

/**
 * Create a file writing tool.
 * 
 * @param workspace_dir Base directory for relative paths
 * @return Configured AgentTool
 */
AgentTool create_write_tool(const std::string& workspace_dir);

/**
 * Create a shell/shell execution tool.
 * 
 * @param workspace_dir Working directory for commands
 * @param timeout_secs Command timeout (default: 20 seconds)
 * @return Configured AgentTool
 */
AgentTool create_shell_tool(const std::string& workspace_dir, int timeout_secs = 20);

/**
 * Create a directory listing tool.
 * 
 * @param workspace_dir Base directory for relative paths
 * @return Configured AgentTool
 */
AgentTool create_list_dir_tool(const std::string& workspace_dir);

/**
 * Create a content chunk retrieval tool.
 * 
 * Used to access parts of large content that was chunked
 * due to size limits.
 * 
 * @param chunker Reference to the ContentChunker instance
 * @return Configured AgentTool
 */
AgentTool create_content_chunk_tool(ContentChunker& chunker);

/**
 * Create a content search tool.
 * 
 * Searches within large chunked content without reading all chunks.
 * 
 * @param chunker Reference to the ContentChunker instance
 * @return Configured AgentTool
 */
AgentTool create_content_search_tool(ContentChunker& chunker);

// ============================================================================
// Path Utilities
// ============================================================================

namespace path {

/**
 * Resolve a path relative to workspace.
 * Handles both absolute and relative paths.
 */
std::string resolve(const std::string& path, const std::string& workspace);

/**
 * Check if a path is within the workspace (security check).
 * Prevents directory traversal attacks.
 */
bool is_within_workspace(const std::string& path, const std::string& workspace);

} // namespace path

} // namespace builtin_tools
} // namespace opencrank

#endif // opencrank_CORE_BUILTIN_TOOLS_HPP
