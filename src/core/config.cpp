#include <opencrank/core/config.hpp>
#include <opencrank/core/logger.hpp>

namespace opencrank {

Config::Config() {}

bool Config::load_file(const std::string& path) {
    std::ifstream f(path.c_str());
    if (!f.is_open()) return false;
    
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    try {
        data_ = Json::parse(content);
        return true;
    } catch (...) {
        return false;
    }
}

bool Config::load_string(const std::string& json_str) {
    try {
        data_ = Json::parse(json_str);
        return true;
    } catch (...) {
        return false;
    }
}

std::string Config::get_string(const std::string& key, const std::string& def) const {
    // Support dot notation for nested keys (e.g., "gateway.bind")
    size_t dot_pos = key.find('.');
    if (dot_pos != std::string::npos) {
        std::string section = key.substr(0, dot_pos);
        std::string subkey = key.substr(dot_pos + 1);
        
        if (data_.contains(section)) {
            const Json& sec = data_[section];
            // Handle multi-level nesting (e.g., "gateway.auth.token")
            size_t dot_pos2 = subkey.find('.');
            if (dot_pos2 != std::string::npos) {
                std::string subsection = subkey.substr(0, dot_pos2);
                std::string finalkey = subkey.substr(dot_pos2 + 1);
                if (sec.contains(subsection)) {
                    const Json& subsec = sec[subsection];
                    if (subsec.contains(finalkey) && subsec[finalkey].is_string()) {
                        LOG_DEBUG("Config: found nested key '%s.%s.%s'", 
                                  section.c_str(), subsection.c_str(), finalkey.c_str());
                        return subsec[finalkey].get<std::string>();
                    }
                }
            } else if (sec.contains(subkey) && sec[subkey].is_string()) {
                LOG_DEBUG("Config: found key '%s.%s'", section.c_str(), subkey.c_str());
                return sec[subkey].get<std::string>();
            }
        }

        LOG_DEBUG("Config: key '%s' not found in section '%s'", subkey.c_str(), section.c_str());
        return def;
    }
    
    // No dot notation, direct lookup
    if (data_.contains(key) && data_[key].is_string()) {
        LOG_DEBUG("Config: found key '%s'", key.c_str());
        return data_[key].get<std::string>();
    }

    LOG_DEBUG("Config: key '%s' not found", key.c_str());
    return def;
}

int64_t Config::get_int(const std::string& key, int64_t def) const {
    // Support dot notation for nested keys
    size_t dot_pos = key.find('.');
    if (dot_pos != std::string::npos) {
        std::string section = key.substr(0, dot_pos);
        std::string subkey = key.substr(dot_pos + 1);
        
        if (data_.contains(section)) {
            const Json& sec = data_[section];
            if (sec.contains(subkey) && sec[subkey].is_number()) {
                LOG_DEBUG("Config: found key '%s.%s'", section.c_str(), subkey.c_str());
                return sec[subkey].get<int64_t>();
            }
        }
        LOG_DEBUG("Config: key '%s' not found in section '%s'", subkey.c_str(), section.c_str());
        return def;
    }
    
    // No dot notation, direct lookup
    if (data_.contains(key) && data_[key].is_number()) {
        LOG_DEBUG("Config: found key '%s'", key.c_str());
        return data_[key].get<int64_t>();
    }
    return def;
}

bool Config::get_bool(const std::string& key, bool def) const {
    // Support dot notation for nested keys
    size_t dot_pos = key.find('.');
    if (dot_pos != std::string::npos) {
        std::string section = key.substr(0, dot_pos);
        std::string subkey = key.substr(dot_pos + 1);
        
        if (data_.contains(section)) {
            const Json& sec = data_[section];
            if (sec.contains(subkey) && sec[subkey].is_boolean()) {
                LOG_DEBUG("Config: found key '%s.%s'", section.c_str(), subkey.c_str());
                return sec[subkey].get<bool>();
            }
        }
        LOG_DEBUG("Config: key '%s' not found in section '%s'", subkey.c_str(), section.c_str());
        return def;
    }
    
    if (data_.contains(key) && data_[key].is_boolean()) {
        LOG_DEBUG("Config: found key '%s'", key.c_str());
        return data_[key].get<bool>();
    }
    return def;
}

const Json& Config::get_section(const std::string& key) const {
    static Json null_json;
    if (data_.contains(key)) {
        return data_[key];
    }
    return null_json;
}

std::string Config::get_channel_string(const std::string& channel, 
                                        const std::string& key,
                                        const std::string& def) const {
    if (data_.contains(channel)) {
        const Json& ch = data_[channel];
        if (ch.contains(key) && ch[key].is_string()) {
            return ch[key].get<std::string>();
        }
    }
    return def;
}

const Json& Config::data() const { return data_; }

void Config::set_string(const std::string& key, const std::string& value) {
    size_t dot_pos = key.find('.');
    if (dot_pos != std::string::npos) {
        std::string section = key.substr(0, dot_pos);
        std::string subkey = key.substr(dot_pos + 1);
        if (!data_.contains(section)) {
            data_[section] = Json::object();
        }
        data_[section][subkey] = value;
    } else {
        data_[key] = value;
    }
}

} // namespace opencrank
