#ifndef opencrank_PLUGIN_REGISTRY_HPP
#define opencrank_PLUGIN_REGISTRY_HPP

#include "plugin.hpp"
#include "channel.hpp"
#include "tool.hpp"
#include "../ai/ai.hpp"
#include "../core/session.hpp"
#include <vector>
#include <map>
#include <string>

namespace opencrank {

// Visibility attribute for plugin-visible symbols
#ifdef __GNUC__
#  define PLUGIN_API __attribute__((visibility("default")))
#else
#  define PLUGIN_API
#endif

// Plugin registry - manages all plugins
class PLUGIN_API PluginRegistry {
public:
    // Singleton instance - must be visible to plugins
    static PluginRegistry& instance();
    
    // Register a plugin
    void register_plugin(Plugin* plugin) {
        if (!plugin) return;
        plugins_.push_back(plugin);
        plugin_map_[plugin->name()] = plugin;
        
        // Also track channel plugins separately
        ChannelPlugin* channel = dynamic_cast<ChannelPlugin*>(plugin);
        if (channel) {
            channels_.push_back(channel);
            channel_map_[channel->channel_id()] = channel;
        }
        
        // Also track tools
        ToolProvider* tool = dynamic_cast<ToolProvider*>(plugin);
        if (tool) {
            tools_.push_back(tool);
            tool_map_[tool->tool_id()] = tool;
        }
        
        // Also track AI plugins
        AIPlugin* ai = dynamic_cast<AIPlugin*>(plugin);
        if (ai) {
            ai_providers_.push_back(ai);
            ai_map_[ai->provider_id()] = ai;
        }
    }
    
    // Get plugin by name
    Plugin* get_plugin(const std::string& name) {
        std::map<std::string, Plugin*>::iterator it = plugin_map_.find(name);
        return it != plugin_map_.end() ? it->second : NULL;
    }
    
    // Get channel by ID
    ChannelPlugin* get_channel(const std::string& channel_id) {
        std::map<std::string, ChannelPlugin*>::iterator it = channel_map_.find(channel_id);
        return it != channel_map_.end() ? it->second : NULL;
    }
    
    // Get tool by ID
    ToolProvider* get_tool(const std::string& tool_id) {
        std::map<std::string, ToolProvider*>::iterator it = tool_map_.find(tool_id);
        return it != tool_map_.end() ? it->second : NULL;
    }
    
    // Get AI provider by ID
    AIPlugin* get_ai(const std::string& provider_id) {
        std::map<std::string, AIPlugin*>::iterator it = ai_map_.find(provider_id);
        return it != ai_map_.end() ? it->second : NULL;
    }
    
    // Get first available AI provider
    AIPlugin* get_default_ai();
    
    // Get all plugins
    const std::vector<Plugin*>& plugins() const { return plugins_; }
    
    // Get all channels
    const std::vector<ChannelPlugin*>& channels() const { return channels_; }
    
    // Get all tools
    const std::vector<ToolProvider*>& tools() const { return tools_; }
    
    // Get all AI providers
    const std::vector<AIPlugin*>& ai_providers() const { return ai_providers_; }
    
    // Initialize all plugins
    bool init_all(const Config& cfg);
    
    // Shutdown all plugins
    void shutdown_all();
    
    // Start all channels
    bool start_all_channels();
    
    // Stop all channels
    void stop_all_channels();
    
    // Poll all channels
    void poll_all_channels();
    
    // Poll all plugins (includes gateway and other services)
    void poll_all();
    
    // Execute a tool action
    ToolResult execute_tool(const std::string& tool_id, 
                            const std::string& action,
                            const Json& params);
    
    // ========== Command Registration ==========
    
    // Register a command from a plugin
    void register_command(const CommandDef& cmd);
    
    // Register multiple commands
    void register_commands(const std::vector<CommandDef>& cmds);
    
    // Find a command handler
    const CommandDef* find_command(const std::string& command) const;
    
    // Execute a command
    std::string execute_command(const std::string& command, const Message& msg, 
                                Session& session, const std::string& args);
    
    // Get all registered commands (for /help)
    const std::map<std::string, CommandDef>& commands() const { return commands_; }

private:
    PluginRegistry() {}
    PluginRegistry(const PluginRegistry&);
    PluginRegistry& operator=(const PluginRegistry&);
    
    std::vector<Plugin*> plugins_;
    std::vector<ChannelPlugin*> channels_;
    std::vector<ToolProvider*> tools_;
    std::vector<AIPlugin*> ai_providers_;
    std::map<std::string, Plugin*> plugin_map_;
    std::map<std::string, ChannelPlugin*> channel_map_;
    std::map<std::string, ToolProvider*> tool_map_;
    std::map<std::string, AIPlugin*> ai_map_;
    std::map<std::string, CommandDef> commands_;
};

} // namespace opencrank

#endif // opencrank_PLUGIN_REGISTRY_HPP
