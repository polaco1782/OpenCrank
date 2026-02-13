/*
 * opencrank C++ - Built-in Agent Tools
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
    AgentToolResult do_notify_user(const Json& params) const;
};

} // namespace opencrank

#endif // opencrank_CORE_BUILTIN_TOOLS_HPP
