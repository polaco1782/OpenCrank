#ifndef opencrank_CORE_CONFIG_HPP
#define opencrank_CORE_CONFIG_HPP

#include "json.hpp"
#include <string>
#include <fstream>

namespace opencrank {

class Config {
public:
    Config();
    
    // Load from JSON file
    bool load_file(const std::string& path);
    
    // Load from JSON string
    bool load_string(const std::string& json_str);
    
    // Get string value
    std::string get_string(const std::string& key, const std::string& def = "") const;
    
    // Set a string value (dot-notation supported for top-level section.key)
    void set_string(const std::string& key, const std::string& value);
    
    int64_t get_int(const std::string& key, int64_t def = 0) const;
    
    bool get_bool(const std::string& key, bool def = false) const;
    
    // Get nested object
    const Json& get_section(const std::string& key) const;
    
    // Channel-specific config helper
    std::string get_channel_string(const std::string& channel, 
                                    const std::string& key,
                                    const std::string& def = "") const;
    
    // Raw data access
    const Json& data() const;

private:
    Json data_;
};

} // namespace opencrank

#endif // opencrank_CORE_CONFIG_HPP
