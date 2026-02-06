/*
 * OpenCrank C++11 - Dynamic Plugin Loader Implementation
 */
#include <opencrank/core/loader.hpp>
#include <opencrank/core/logger.hpp>
#include <dlfcn.h>
#include <opencrank/core/dl_utils.hpp>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>

namespace opencrank {

namespace {
    void teardown_loaded_plugin(LoadedPlugin& plugin) {
        if (plugin.instance) {
            plugin.instance->shutdown();
            if (plugin.destroy_func) {
                plugin.destroy_func(plugin.instance);
            }
            plugin.instance = nullptr;
        }

        if (plugin.handle) {
            dlclose(plugin.handle);
            plugin.handle = nullptr;
        }
    }
}

PluginLoader::PluginLoader() {
    // Default search paths
    search_paths_.push_back("./plugins");
    search_paths_.push_back("/usr/lib/opencrank/plugins");
    search_paths_.push_back("/usr/local/lib/opencrank/plugins");
}

PluginLoader::~PluginLoader() {
    unload_all();
}

bool PluginLoader::load(const std::string& path) {
    // Check if it's a full path or just a name
    std::string full_path;
    struct stat st;
    
    if (path.find('/') != std::string::npos && stat(path.c_str(), &st) == 0) {
        full_path = path;
    } else {
        full_path = find_plugin(path);
        if (full_path.empty()) {
            set_error("Plugin not found: " + path);
            return false;
        }
    }
    
    LoadedPlugin plugin;
    if (!load_impl(full_path, plugin)) {
        return false;
    }
    
    // Check for duplicate
    if (name_index_.find(plugin.info.name) != name_index_.end()) {
        LOG_WARN("Plugin %s already loaded, skipping", plugin.info.name);
        if (plugin.destroy_func && plugin.instance) {
            plugin.destroy_func(plugin.instance);
        }
        dlclose(plugin.handle);
        return true;  // Not an error
    }
    
    name_index_[plugin.info.name] = plugins_.size();
    plugins_.push_back(plugin);
    
    LOG_INFO("Loaded plugin: %s v%s (%s)", 
             plugin.info.name, plugin.info.version, plugin.info.type);
    
    return true;
}

bool PluginLoader::load_impl(const std::string& path, LoadedPlugin& plugin) {
    // Open shared library
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        set_error(std::string("dlopen failed: ") + dlerror());
        return false;
    }
    
    plugin.handle = handle;
    plugin.path = path;
    
    // Lookup required symbols (use templated helper to avoid repetitive casts)
    GetPluginInfoFunc get_info = get_symbol<GetPluginInfoFunc>(handle, "opencrank_get_plugin_info");
    if (!get_info) {
        set_error("Plugin missing opencrank_get_plugin_info: " + path);
        dlclose(handle);
        return false;
    }

    plugin.create_func = get_symbol<CreatePluginFunc>(handle, "opencrank_create_plugin");
    plugin.destroy_func = get_symbol<DestroyPluginFunc>(handle, "opencrank_destroy_plugin");

    if (!plugin.create_func) {
        set_error("Plugin missing opencrank_create_plugin: " + path);
        dlclose(handle);
        return false;
    }
    
    // Get plugin info
    plugin.info = get_info();
    
    // Create plugin instance
    plugin.instance = plugin.create_func();
    if (!plugin.instance) {
        set_error("Failed to create plugin instance: " + path);
        dlclose(handle);
        return false;
    }
    
    return true;
}

int PluginLoader::load_dir(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (!d) {
        return 0;
    }
    
    int count = 0;
    struct dirent* entry;
    
    while ((entry = readdir(d)) != nullptr) {
        std::string name = entry->d_name;
        
        // Skip hidden files and non-.so files
        if (name[0] == '.') continue;
        if (name.length() < 4) continue;
        if (name.substr(name.length() - 3) != ".so") continue;
        
        std::string path = dir + "/" + name;
        if (load(path)) {
            count++;
        }
    }
    
    closedir(d);
    return count;
}

int PluginLoader::load_from_config(const Config& config) {
    int count = 0;
    
    // Get plugin list from config
    const Json& plugins_config = config.get_section("plugins");
    
    if (plugins_config.is_array()) {
        // Array of plugin names/paths
        for (const auto& item : plugins_config) {
            if (item.is_string()) {
                std::string name = item.get<std::string>();
                if (!name.empty() && load(name)) {
                    count++;
                }
            }
        }
    } else if (plugins_config.is_object()) {
        // Object with plugin names as keys, config as values
        for (auto it = plugins_config.begin(); it != plugins_config.end(); ++it) {
            // Check if plugin is enabled
            const Json& plugin_cfg = it.value();
            if (plugin_cfg.is_object()) {
                bool enabled = true;
                if (plugin_cfg.contains("enabled")) {
                    enabled = plugin_cfg["enabled"].get<bool>();
                }
                if (!enabled) {
                    LOG_DEBUG("Plugin %s disabled in config", it.key().c_str());
                    continue;
                }
            }
            
            if (load(it.key())) {
                count++;
            }
        }
    }
    
    // Note: plugins_dir is used as a search path, not for auto-loading
    // Only plugins explicitly listed in "plugins" array/object will be loaded
    
    return count;
}

void PluginLoader::unload(const std::string& name) {
    std::map<std::string, size_t>::iterator it = name_index_.find(name);
    if (it == name_index_.end()) {
        return;
    }
    
    size_t idx = it->second;
    LoadedPlugin& plugin = plugins_[idx];
    teardown_loaded_plugin(plugin);
    
    LOG_INFO("Unloaded plugin: %s", name.c_str());
    
    // Remove from maps (mark as unloaded but keep in vector to preserve indices)
    name_index_.erase(it);
}

void PluginLoader::unload_all() {
    // Unload in reverse order
    for (size_t i = plugins_.size(); i > 0; --i) {
        LoadedPlugin& plugin = plugins_[i - 1];
        teardown_loaded_plugin(plugin);
    }
    
    plugins_.clear();
    name_index_.clear();
}

Plugin* PluginLoader::get(const std::string& name) {
    std::map<std::string, size_t>::iterator it = name_index_.find(name);
    if (it == name_index_.end()) {
        return nullptr;
    }
    return plugins_[it->second].instance;
}

const std::vector<LoadedPlugin>& PluginLoader::plugins() const {
    return plugins_;
}

bool PluginLoader::is_loaded(const std::string& name) const {
    return name_index_.find(name) != name_index_.end();
}

std::string PluginLoader::last_error() const {
    return last_error_;
}

const std::vector<std::string>& PluginLoader::search_paths() const {
    return search_paths_;
}

void PluginLoader::add_search_path(const std::string& path) {
    search_paths_.insert(search_paths_.begin(), path);
}

std::string PluginLoader::find_plugin(const std::string& name) {
    // Try various name formats
    const std::vector<std::string> candidates = {
        name,
        name + ".so",
        std::string("lib") + name + ".so",
        std::string("opencrank_") + name + ".so",
        std::string("libopencrank_") + name + ".so"
    };

    struct stat st;
    for (const auto& base : search_paths_) {
        for (const auto& cand : candidates) {
            std::string path = base + "/" + cand;
            if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                return path;
            }
        }
    }
    
    return "";
}

void PluginLoader::set_error(const std::string& error) {
    last_error_ = error;
    LOG_ERROR("%s", error.c_str());
}

} // namespace opencrank
