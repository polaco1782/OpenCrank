/*
 * OpenCrank C++11 - Dynamic Plugin Loader
 * 
 * Loads plugins from shared libraries (.so files) at runtime.
 * Plugins must export a create_plugin() function.
 */
#ifndef OPENCRANK_PLUGIN_LOADER_HPP
#define OPENCRANK_PLUGIN_LOADER_HPP

#include "plugin.hpp"
#include "../core/config.hpp"
#include <string>
#include <vector>
#include <map>

namespace opencrank {

// Plugin metadata returned by shared library
struct PluginInfo {
    const char* name;
    const char* version;
    const char* description;
    const char* type;  // "channel", "tool", "ai"
};

// Function signatures that plugins must export
typedef PluginInfo (*GetPluginInfoFunc)();
typedef Plugin* (*CreatePluginFunc)();
typedef void (*DestroyPluginFunc)(Plugin*);

// Loaded plugin handle
struct LoadedPlugin {
    void* handle;                   // dlopen handle
    std::string path;               // Path to .so file
    PluginInfo info;                // Plugin metadata
    Plugin* instance;               // Plugin instance
    CreatePluginFunc create_func;
    DestroyPluginFunc destroy_func;
    
    LoadedPlugin() : handle(nullptr), instance(nullptr), 
                     create_func(nullptr), destroy_func(nullptr) {}
};

// Dynamic plugin loader
class PluginLoader {
public:
    PluginLoader();
    ~PluginLoader();
    
    // Load a single plugin from .so file
    bool load(const std::string& path);
    
    // Load plugins from directory
    int load_dir(const std::string& dir);
    
    // Load plugins specified in config
    int load_from_config(const Config& config);
    
    // Unload a specific plugin
    void unload(const std::string& name);
    
    // Unload all plugins
    void unload_all();
    
    // Get loaded plugin by name
    Plugin* get(const std::string& name);
    
    // Get all loaded plugins
    const std::vector<LoadedPlugin>& plugins() const;
    
    // Check if a plugin is loaded
    bool is_loaded(const std::string& name) const;
    
    // Get last error message
    std::string last_error() const;
    
    // Get plugin search paths
    const std::vector<std::string>& search_paths() const;
    
    // Add plugin search path
    void add_search_path(const std::string& path);

private:
    std::vector<LoadedPlugin> plugins_;
    std::map<std::string, size_t> name_index_;
    std::vector<std::string> search_paths_;
    std::string last_error_;
    
    bool load_impl(const std::string& path, LoadedPlugin& plugin);
    std::string find_plugin(const std::string& name);
    void set_error(const std::string& error);
};

} // namespace opencrank

// Macros for plugin authors to export required functions
#define OPENCRANK_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))

#define OPENCRANK_DECLARE_PLUGIN(PluginClass, plugin_name, plugin_version, plugin_desc, plugin_type) \
    OPENCRANK_PLUGIN_EXPORT opencrank::PluginInfo opencrank_get_plugin_info() { \
        opencrank::PluginInfo info; \
        info.name = plugin_name; \
        info.version = plugin_version; \
        info.description = plugin_desc; \
        info.type = plugin_type; \
        return info; \
    } \
    OPENCRANK_PLUGIN_EXPORT opencrank::Plugin* opencrank_create_plugin() { \
        return new PluginClass(); \
    } \
    OPENCRANK_PLUGIN_EXPORT void opencrank_destroy_plugin(opencrank::Plugin* plugin) { \
        delete plugin; \
    }

#endif // OPENCRANK_PLUGIN_LOADER_HPP
