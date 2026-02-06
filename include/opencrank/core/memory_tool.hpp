/*
 * opencrank C++11 - Memory Tool (Core)
 * 
 * Provides memory_save, memory_search, memory_get, and task operations.
 * Part of core functionality for use by other plugins.
 */
#ifndef opencrank_CORE_MEMORY_TOOL_HPP
#define opencrank_CORE_MEMORY_TOOL_HPP

#include <opencrank/core/tool.hpp>
#include <opencrank/core/agent.hpp>
#include <opencrank/memory/manager.hpp>
#include <memory>
#include <string>

namespace opencrank {

class MemoryTool : public ToolProvider {
public:
    MemoryTool();
    ~MemoryTool();
    
    // Lifecycle
    bool init(const Config& cfg);
    void shutdown();
    bool is_initialized() const { return initialized_; }
    
    // Metadata
    const char* name() const;
    const char* description() const;
    const char* version() const;

    // Tool interface
    const char* tool_id() const;
    std::vector<std::string> actions() const;
    
    // Agent tools with detailed descriptions
    std::vector<AgentTool> get_agent_tools() const;

    ToolResult execute(const std::string& action, const Json& params);
    
    // Tool schema for AI function calling
    Json get_tool_schema() const;
    
    // Execute tool call (internal)
    Json execute_function(const std::string& function_name, const Json& params);
    
    // Direct access to memory manager for other core components
    MemoryManager* memory_manager();
    const MemoryManager* memory_manager() const;
    
private:
    std::unique_ptr<MemoryManager> manager_;
    MemoryConfig config_;
    // Tool implementations
    Json memory_save(const Json& params);
    Json memory_search(const Json& params);
    Json memory_get(const Json& params);
    Json memory_list(const Json& params);
    Json task_create(const Json& params);
    Json task_complete(const Json& params);
    Json task_list(const Json& params);
    
    Json make_error(const std::string& message);
    Json make_success(const std::string& message);
};

} // namespace opencrank

#endif // opencrank_CORE_MEMORY_TOOL_HPP
