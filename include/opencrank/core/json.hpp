#ifndef opencrank_CORE_JSON_HPP
#define opencrank_CORE_JSON_HPP

/*
 * opencrank JSON wrapper using nlohmann/json
 * 
 * This provides a thin wrapper around nlohmann/json that maintains
 * API compatibility with the codebase while leveraging the robust,
 * UTF-8 compliant nlohmann library.
 */

#include "deps/nlohmann_json.hpp"
#include <string>
#include <vector>
#include <map>

namespace opencrank {

// Type alias for nlohmann::json
using Json = nlohmann::json;

// Convenience functions for common operations (backwards compatibility)
namespace json_utils {

// Check if JSON object has a key
inline bool has(const Json& j, const std::string& key) {
    return j.is_object() && j.contains(key);
}

// Safe string extraction with default
inline std::string get_string(const Json& j, const std::string& key, const std::string& def = "") {
    if (j.is_object() && j.contains(key)) {
        const auto& val = j[key];
        if (val.is_string()) {
            return val.get<std::string>();
        }
    }
    return def;
}

// Safe int extraction with default
inline int64_t get_int(const Json& j, const std::string& key, int64_t def = 0) {
    if (j.is_object() && j.contains(key)) {
        const auto& val = j[key];
        if (val.is_number_integer()) {
            return val.get<int64_t>();
        } else if (val.is_number()) {
            return static_cast<int64_t>(val.get<double>());
        }
    }
    return def;
}

// Safe double extraction with default
inline double get_double(const Json& j, const std::string& key, double def = 0.0) {
    if (j.is_object() && j.contains(key)) {
        const auto& val = j[key];
        if (val.is_number()) {
            return val.get<double>();
        }
    }
    return def;
}

// Safe bool extraction with default
inline bool get_bool(const Json& j, const std::string& key, bool def = false) {
    if (j.is_object() && j.contains(key)) {
        const auto& val = j[key];
        if (val.is_boolean()) {
            return val.get<bool>();
        }
    }
    return def;
}

// Safe value extraction with defaults (for any type)
inline std::string as_string(const Json& j, const std::string& def = "") {
    if (j.is_string()) {
        return j.get<std::string>();
    }
    return def;
}

inline int64_t as_int(const Json& j, int64_t def = 0) {
    if (j.is_number_integer()) {
        return j.get<int64_t>();
    } else if (j.is_number()) {
        return static_cast<int64_t>(j.get<double>());
    }
    return def;
}

inline double as_number(const Json& j, double def = 0.0) {
    if (j.is_number()) {
        return j.get<double>();
    }
    return def;
}

inline bool as_bool(const Json& j, bool def = false) {
    if (j.is_boolean()) {
        return j.get<bool>();
    }
    return def;
}

// Create empty object
inline Json object() {
    return Json::object();
}

// Create empty array
inline Json array() {
    return Json::array();
}

} // namespace json_utils

} // namespace opencrank

#endif // opencrank_CORE_JSON_HPP
