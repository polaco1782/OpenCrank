#ifndef opencrank_PLUGIN_TOOL_HPP
#define opencrank_PLUGIN_TOOL_HPP

#include "plugin.hpp"
#include "../core/json.hpp"
#include <string>
#include <vector>

namespace opencrank {

// Forward declaration
struct AgentTool;

// Tool result structure
struct ToolResult {
    bool success;
    Json data;
    std::string error;
    
    ToolResult() : success(false) {}
    
    static ToolResult ok(const Json& result) {
        ToolResult r;
        r.success = true;
        r.data = result;
        return r;
    }
    
    static ToolResult fail(const std::string& err) {
        ToolResult r;
        r.success = false;
        r.error = err;
        return r;
    }
};

// Tool interface - for agent tools (browser, search, etc.)
class ToolProvider : public Plugin {
public:
    virtual ~ToolProvider() {}
    
    // Tool metadata
    virtual const char* tool_id() const = 0;
    virtual std::vector<std::string> actions() const = 0;
    
    // Execute an action
    virtual ToolResult execute(const std::string& action, const Json& params) = 0;
    
    // Get agent tools with full descriptions for each action
    // Override this to provide detailed tool descriptions to the AI
    virtual std::vector<AgentTool> get_agent_tools() const;
    
    // Check if action is supported
    bool supports(const std::string& action) const {
        std::vector<std::string> acts = actions();
        for (size_t i = 0; i < acts.size(); ++i) {
            if (acts[i] == action) return true;
        }
        return false;
    }
};

} // namespace opencrank

#endif // opencrank_PLUGIN_TOOL_HPP
