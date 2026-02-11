/*
 * opencrank C++11 - Memory Tool
 * 
 * ToolProvider that exposes memory, file, and task operations to the agent.
 * 
 * Agent-facing actions:
 *   memory_save    - Save information to persistent database
 *   memory_search  - Search memories using BM25 full-text search
 *   memory_get     - Retrieve a specific memory or recent entries
 *   file_save      - Save content to a file in the workspace
 *   file_read      - Read a file from the workspace
 *   task_create    - Create a new task/reminder
 *   task_list      - List active tasks
 *   task_complete  - Mark a task as completed
 * 
 * Memory and tasks are ONLY database read/write.
 * File operations use the workspace filesystem.
 */
#ifndef opencrank_CORE_MEMORY_TOOL_HPP
#define opencrank_CORE_MEMORY_TOOL_HPP

#include "tool.hpp"
#include "agent.hpp"
#include <opencrank/memory/manager.hpp>
#include <string>

namespace opencrank {

class MemoryTool : public ToolProvider {
public:
    MemoryTool();
    virtual ~MemoryTool();
    
    // Plugin interface
    const char* name() const override { return "memory"; }
    const char* description() const override {
        return "Persistent memory, file, and task management tools";
    }
    const char* version() const override { return "1.0.0"; }
    
    bool init(const Config& cfg) override;
    void shutdown() override;
    
    // ToolProvider interface
    const char* tool_id() const override { return "memory"; }
    std::vector<std::string> actions() const override;
    ToolResult execute(const std::string& action, const Json& params) override;
    
    // Agent tools with detailed descriptions
    std::vector<AgentTool> get_agent_tools() const override;
    
    // Access to the underlying manager
    MemoryManager& manager() { return manager_; }
    const MemoryManager& manager() const { return manager_; }

private:
    MemoryManager manager_;
    std::string workspace_dir_;
    
    // Tool action implementations
    ToolResult do_memory_save(const Json& params);
    ToolResult do_memory_search(const Json& params);
    ToolResult do_memory_get(const Json& params);
    ToolResult do_file_save(const Json& params);
    ToolResult do_file_read(const Json& params);
    ToolResult do_task_create(const Json& params);
    ToolResult do_task_list(const Json& params);
    ToolResult do_task_complete(const Json& params);
    
    // Helper: resolve a path relative to workspace
    std::string resolve_path(const std::string& path) const;
    
    // Helper: ensure directory exists for a file path
    bool ensure_parent_dir(const std::string& filepath) const;
    
    // Helper: get daily file path (memory/YYYY-MM-DD.md)
    std::string daily_file_path() const;
};

} // namespace opencrank

#endif // opencrank_CORE_MEMORY_TOOL_HPP
