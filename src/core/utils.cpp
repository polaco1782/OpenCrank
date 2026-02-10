#include <opencrank/core/utils.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <openssl/rand.h>

namespace opencrank {

// ============ Math utilities ============

int clamp_int(int value, int min_val, int max_val) {
    return clamp(value, min_val, max_val);
}

// ============ Time utilities ============

void sleep_ms(int milliseconds) {
    if (milliseconds <= 0) return;
    usleep(static_cast<useconds_t>(milliseconds) * 1000);
}

int64_t current_timestamp() {
    return static_cast<int64_t>(std::time(NULL));
}

int64_t current_timestamp_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

std::string format_timestamp(int64_t timestamp) {
    time_t t = static_cast<time_t>(timestamp);
    struct tm tm_buf;
    gmtime_r(&t, &tm_buf);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return std::string(buf);
}

std::string format_date(int64_t timestamp) {
    time_t t = static_cast<time_t>(timestamp);
    struct tm tm_buf;
    gmtime_r(&t, &tm_buf);
    char buf[16];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
    return std::string(buf);
}

// ============ String utilities ============

std::string trim(const std::string& s) {
    return rtrim(ltrim(s));
}

std::string ltrim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    return s.substr(start);
}

std::string rtrim(const std::string& s) {
    size_t end = s.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(0, end);
}

std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::string to_upper(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

bool starts_with(const std::string& s, const std::string& prefix) {
    if (prefix.size() > s.size()) return false;
    return s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string& s, const std::string& suffix) {
    if (suffix.size() > s.size()) return false;
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> parts;
    std::istringstream iss(s);
    std::string part;
    while (std::getline(iss, part, delimiter)) {
        parts.push_back(part);
    }
    return parts;
}

std::vector<std::string> split(const std::string& s, const std::string& delimiter) {
    std::vector<std::string> parts;
    if (delimiter.empty()) {
        parts.push_back(s);
        return parts;
    }
    size_t start = 0;
    size_t end;
    while ((end = s.find(delimiter, start)) != std::string::npos) {
        parts.push_back(s.substr(start, end - start));
        start = end + delimiter.size();
    }
    parts.push_back(s.substr(start));
    return parts;
}

std::string join(const std::vector<std::string>& parts, const std::string& delimiter) {
    if (parts.empty()) return "";
    std::ostringstream oss;
    oss << parts[0];
    for (size_t i = 1; i < parts.size(); ++i) {
        oss << delimiter << parts[i];
    }
    return oss.str();
}

std::string replace_all(const std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    std::string result = s;
    size_t pos = 0;
    while ((pos = result.find(from, pos)) != std::string::npos) {
        result.replace(pos, from.size(), to);
        pos += to.size();
    }
    return result;
}

std::string truncate_safe(const std::string& s, size_t max_len) {
    if (s.size() <= max_len) return s;
    
    // Find a safe truncation point (don't break UTF-8 multi-byte sequences)
    size_t len = max_len;
    while (len > 0 && (static_cast<unsigned char>(s[len]) & 0xC0) == 0x80) {
        --len;  // Back up if in the middle of a multi-byte sequence
    }
    return s.substr(0, len);
}

std::string sanitize_utf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        
        // Determine expected byte count from leading byte
        int expected = 0;
        if (c < 0x80) {
            // ASCII range: allow tab (0x09), newline (0x0A), and printable chars
            // Strip carriage return (0x0D) and other control chars
            if (c == 0x09 || c == 0x0A || c >= 0x20) {
                out.push_back(static_cast<char>(c));
            }
            // else: skip control character (including 0x0D \r)
            ++i;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            expected = 2;
        } else if ((c & 0xF0) == 0xE0) {
            expected = 3;
        } else if ((c & 0xF8) == 0xF0) {
            expected = 4;
        } else {
            // Invalid leading byte, skip
            ++i;
            continue;
        }
        
        // Validate continuation bytes
        bool valid = true;
        if (i + expected > s.size()) {
            valid = false;
        } else {
            for (int j = 1; j < expected; ++j) {
                if ((static_cast<unsigned char>(s[i + j]) & 0xC0) != 0x80) {
                    valid = false;
                    break;
                }
            }
        }
        
        if (valid) {
            for (int j = 0; j < expected; ++j) {
                out.push_back(s[i + j]);
            }
            i += expected;
        } else {
            // Skip invalid byte
            ++i;
        }
    }
    
    return out;
}

// ============ Phone number utilities ============

std::string normalize_e164(const std::string& number) {
    std::string stripped = without_whatsapp_prefix(number);
    stripped = trim(stripped);
    
    // Extract only digits and leading +
    std::string result;
    bool has_plus = !stripped.empty() && stripped[0] == '+';
    
    for (size_t i = 0; i < stripped.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(stripped[i]))) {
            result += stripped[i];
        }
    }
    
    if (result.empty()) return "";
    
    // Ensure starts with +
    if (has_plus || result[0] != '+') {
        result = "+" + result;
    }
    
    return result;
}

std::string with_whatsapp_prefix(const std::string& number) {
    if (starts_with(number, "whatsapp:")) return number;
    return "whatsapp:" + number;
}

std::string without_whatsapp_prefix(const std::string& number) {
    if (starts_with(number, "whatsapp:")) {
        return number.substr(9);
    }
    return number;
}

std::string jid_to_e164(const std::string& jid) {
    // Format: 1234567890@s.whatsapp.net or 1234567890:1@s.whatsapp.net
    size_t at_pos = jid.find('@');
    if (at_pos == std::string::npos) return "";
    
    std::string prefix = jid.substr(0, at_pos);
    
    // Remove device suffix (e.g., :1)
    size_t colon_pos = prefix.find(':');
    if (colon_pos != std::string::npos) {
        prefix = prefix.substr(0, colon_pos);
    }
    
    // Check if it's a valid phone JID
    if (jid.find("@s.whatsapp.net") == std::string::npos &&
        jid.find("@hosted") == std::string::npos) {
        return "";  // Not a phone number JID
    }
    
    // Verify all digits
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(prefix[i]))) {
            return "";
        }
    }
    
    return "+" + prefix;
}

std::string e164_to_jid(const std::string& e164) {
    std::string digits;
    for (size_t i = 0; i < e164.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(e164[i]))) {
            digits += e164[i];
        }
    }
    if (digits.empty()) return "";
    return digits + "@s.whatsapp.net";
}

// ============ Path utilities ============

std::string get_home_dir() {
    const char* home = std::getenv("HOME");
    if (home && home[0]) return std::string(home);
    
    home = std::getenv("USERPROFILE");
    if (home && home[0]) return std::string(home);
    
    return "";
}

std::string resolve_user_path(const std::string& path) {
    std::string trimmed = trim(path);
    if (trimmed.empty()) return trimmed;
    
    // Expand ~ to home directory
    if (trimmed[0] == '~') {
        std::string home = get_home_dir();
        if (trimmed.size() == 1) {
            return home;
        }
        if (trimmed[1] == '/' || trimmed[1] == '\\') {
            return home + trimmed.substr(1);
        }
    }
    
    return trimmed;
}

std::string normalize_path(const std::string& path) {
    if (path.empty()) return path;
    
    std::vector<std::string> parts = split(path, '/');
    std::vector<std::string> result;
    
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].empty() || parts[i] == ".") {
            continue;
        }
        if (parts[i] == "..") {
            if (!result.empty() && result.back() != "..") {
                result.pop_back();
            } else if (path[0] != '/') {
                result.push_back("..");
            }
        } else {
            result.push_back(parts[i]);
        }
    }
    
    std::string normalized = join(result, "/");
    if (path[0] == '/') {
        normalized = "/" + normalized;
    }
    
    return normalized.empty() ? "." : normalized;
}

std::string join_path(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    
    bool a_ends_slash = !a.empty() && a.back() == '/';
    bool b_starts_slash = !b.empty() && b[0] == '/';
    
    if (a_ends_slash && b_starts_slash) {
        return a + b.substr(1);
    }
    if (!a_ends_slash && !b_starts_slash) {
        return a + "/" + b;
    }
    return a + b;
}

std::string basename(const std::string& path) {
    size_t pos = path.rfind('/');
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

std::string dirname(const std::string& path) {
    size_t pos = path.rfind('/');
    if (pos == std::string::npos) return ".";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

bool path_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool is_directory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

bool mkdir_p(const std::string& path) {
    if (path.empty()) return false;
    
    std::vector<std::string> parts = split(path, '/');
    std::string current;
    
    if (path[0] == '/') {
        current = "/";
    }
    
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].empty()) continue;
        current = join_path(current, parts[i]);
        
        if (!path_exists(current)) {
            if (mkdir(current.c_str(), 0755) != 0) {
                return false;
            }
        } else if (!is_directory(current)) {
            return false;
        }
    }
    
    return true;
}

std::string shorten_home_path(const std::string& path) {
    std::string home = get_home_dir();
    if (home.empty()) return path;
    
    if (path == home) return "~";
    
    if (starts_with(path, home + "/")) {
        return "~" + path.substr(home.size());
    }
    
    return path;
}

// ============ UUID utilities ============

std::string generate_uuid() {
    unsigned char bytes[16];
    RAND_bytes(bytes, 16);
    
    // Set version 4
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    // Set variant
    bytes[8] = (bytes[8] & 0x3F) | 0x80;
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            oss << '-';
        }
        oss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    
    return oss.str();
}

// ============ Hashing utilities ============

std::string sha256_hex(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.c_str()), data.size(), hash);
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }
    return oss.str();
}

std::string md5_hex(const std::string& data) {
    unsigned char hash[MD5_DIGEST_LENGTH];
    MD5(reinterpret_cast<const unsigned char*>(data.c_str()), data.size(), hash);
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }
    return oss.str();
}

// ============ HTML utilities ============

std::string strip_html_for_ai(const std::string& html) {
    std::string result;
    result.reserve(html.size());

    bool in_tag = false;
    bool in_script = false;
    bool in_style = false;
    bool keep_tag = false;
    std::string tag_buffer;

    for (size_t i = 0; i < html.size(); ++i) {
        char c = html[i];

        if (c == '<') {
            in_tag = true;
            tag_buffer = "<";

            // Peek ahead to identify the tag name
            std::string peek;
            for (size_t j = i + 1; j < html.size() && j < i + 12; ++j) {
                if (html[j] == ' ' || html[j] == '>' || html[j] == '\t' || html[j] == '\n') break;
                peek += static_cast<char>(tolower(html[j]));
            }

            // Track script/style blocks
            if (peek == "script" || peek.find("script") == 0) {
                in_script = true;
            } else if (peek == "/script") {
                in_script = false;
            } else if (peek == "style" || peek.find("style") == 0) {
                in_style = true;
            } else if (peek == "/style") {
                in_style = false;
            }

            // Preserve <a> and <img> tags
            keep_tag = (peek == "a" || peek == "img" || peek == "/a" || peek == "/img");

        } else if (c == '>' && in_tag) {
            tag_buffer += c;
            in_tag = false;

            if (keep_tag && !in_script && !in_style) {
                result += tag_buffer;
            } else if (!in_script && !in_style) {
                // Replace block-level tag boundaries with newlines for readability
                result += ' ';
            }

            tag_buffer.clear();
            keep_tag = false;

        } else if (in_tag) {
            tag_buffer += c;

        } else if (!in_script && !in_style) {
            // Decode common HTML entities
            if (c == '&' && i + 1 < html.size()) {
                std::string entity;
                size_t j = i + 1;
                while (j < html.size() && j < i + 10 && html[j] != ';' && html[j] != ' ') {
                    entity += html[j];
                    ++j;
                }

                if (j < html.size() && html[j] == ';') {
                    std::string decoded;
                    if (entity == "nbsp" || entity == "#160") decoded = " ";
                    else if (entity == "amp" || entity == "#38") decoded = "&";
                    else if (entity == "lt" || entity == "#60") decoded = "<";
                    else if (entity == "gt" || entity == "#62") decoded = ">";
                    else if (entity == "quot" || entity == "#34") decoded = "\"";
                    else if (entity == "apos" || entity == "#39") decoded = "'";
                    else if (entity == "mdash" || entity == "#8212") decoded = "--";
                    else if (entity == "ndash" || entity == "#8211") decoded = "-";
                    else if (entity == "hellip" || entity == "#8230") decoded = "...";
                    else if (entity == "laquo" || entity == "#171") decoded = "<<";
                    else if (entity == "raquo" || entity == "#187") decoded = ">>";

                    if (!decoded.empty()) {
                        result += decoded;
                        i = j;
                        continue;
                    }
                }
            }
            result += c;
        }
    }

    return normalize_whitespace(result);
}

std::string normalize_whitespace(const std::string& s) {
    std::string result;
    result.reserve(s.size());

    bool last_was_space = true;  // Start true to trim leading

    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        bool is_space = (c == ' ' || c == '\t' || c == '\n' || c == '\r');

        if (is_space) {
            if (!last_was_space) {
                result += ' ';
                last_was_space = true;
            }
        } else {
            result += c;
            last_was_space = false;
        }
    }

    // Trim trailing
    if (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }

    return result;
}

} // namespace opencrank
