/*
 * OpenCrank C++11 - Plugin Registry Implementation
 */
#include <opencrank/core/registry.hpp>
#include <opencrank/core/logger.hpp>

namespace opencrank {

// Singleton instance in .cpp file to ensure single instance across all plugins
// Must be visible to plugins, so don't use hidden visibility
#ifdef __GNUC__
__attribute__((visibility("default")))
#endif
PluginRegistry& PluginRegistry::instance() {
    static PluginRegistry registry;
    return registry;
}

void PluginRegistry::register_command(const CommandDef& cmd) {
    if (cmd.command.empty() || !cmd.handler) {
        LOG_WARN("Attempted to register invalid command (empty=%d, handler=%p)", 
                 cmd.command.empty(), (void*)cmd.handler);
        return;
    }
    commands_[cmd.command] = cmd;
    LOG_DEBUG("  -> Registered: %s", cmd.command.c_str());
}

void PluginRegistry::register_commands(const std::vector<CommandDef>& cmds) {
    LOG_DEBUG("[Registry] registering %zu commands", cmds.size());
    for (size_t i = 0; i < cmds.size(); ++i) {
        register_command(cmds[i]);
    }
    LOG_DEBUG("[Registry] total commands now: %zu", commands_.size());
}

std::string PluginRegistry::execute_command(const std::string& command, const Message& msg, 
                                            Session& session, const std::string& args) {
    const CommandDef* cmd = find_command(command);
    if (!cmd || !cmd->handler) {
        return "";  // Command not found
    }
    return cmd->handler(msg, session, args);
}

const CommandDef* PluginRegistry::find_command(const std::string& command) const {
    std::map<std::string, CommandDef>::const_iterator it = commands_.find(command);
    return it != commands_.end() ? &it->second : NULL;
}

bool PluginRegistry::init_all(const Config& cfg) {
    bool all_ok = true;
    for (size_t i = 0; i < plugins_.size(); ++i) {
        if (!plugins_[i]->init(cfg)) {
            all_ok = false;
        }
    }
    return all_ok;
}

void PluginRegistry::shutdown_all() {
    // Shutdown in reverse order
    for (size_t i = plugins_.size(); i > 0; --i) {
        plugins_[i - 1]->shutdown();
    }
}

bool PluginRegistry::start_all_channels() {
    bool all_ok = true;
    for (size_t i = 0; i < channels_.size(); ++i) {
        if (!channels_[i]->start()) {
            all_ok = false;
        }
    }
    return all_ok;
}

void PluginRegistry::stop_all_channels() {
    for (size_t i = 0; i < channels_.size(); ++i) {
        channels_[i]->stop();
    }
}

void PluginRegistry::poll_all_channels() {
    for (size_t i = 0; i < channels_.size(); ++i) {
        channels_[i]->poll();
    }
}

void PluginRegistry::poll_all() {
    for (size_t i = 0; i < plugins_.size(); ++i) {
        plugins_[i]->poll();
    }
}

ToolResult PluginRegistry::execute_tool(const std::string& tool_id, 
                                        const std::string& action,
                                        const Json& params) {
    ToolProvider* tool = get_tool(tool_id);
    if (!tool) {
        return ToolResult::fail("Tool not found: " + tool_id);
    }
    if (!tool->supports(action)) {
        return ToolResult::fail("Tool " + tool_id + " does not support action: " + action);
    }
    return tool->execute(action, params);
}

AIPlugin* PluginRegistry::get_default_ai() {
    LOG_DEBUG("[Registry] Selecting default AI provider from %zu registered providers", ai_providers_.size());
    for (size_t i = 0; i < ai_providers_.size(); ++i) {
        LOG_DEBUG("[Registry]   Checking: %s (initialized=%d, configured=%d)", 
                  ai_providers_[i]->provider_id().c_str(),
                  ai_providers_[i]->is_initialized(),
                  ai_providers_[i]->is_configured());
        if (ai_providers_[i]->is_initialized() && ai_providers_[i]->is_configured()) {
            LOG_INFO("[Registry] Selected AI provider: %s", ai_providers_[i]->provider_id().c_str());
            return ai_providers_[i];
        }
    }
    LOG_WARN("[Registry] No configured AI provider found");
    return NULL;
}

} // namespace opencrank
