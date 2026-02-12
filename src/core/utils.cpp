#include <opencrank/core/utils.hpp>
#include <algorithm>
#include <numeric>
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
#include <errno.h>
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#include <string_view>

namespace opencrank {

// ============ Math utilities ============

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

// ============ String utilities ============

std::string trim(const std::string& s) {
    return rtrim(ltrim(s));
}

std::string ltrim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    return s.substr(start);
}

std::string rtrim(const std::string& s) {
    size_t end = s.find_last_not_of(" \t\n\r");
    if (end == std::string::npos) return "";
    return s.substr(0, end + 1);
}

std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && 
           std::equal(prefix.begin(), prefix.end(), s.begin());
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
    return std::accumulate(
        std::next(parts.begin()), parts.end(), parts[0],
        [&](const std::string& a, const std::string& b) {
            return a + delimiter + b;
        });
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
            } else {
                // Replace control character with space or skip
                out.push_back(' ');
            }
            ++i;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            expected = 2;
        } else if ((c & 0xF0) == 0xE0) {
            expected = 3;
        } else if ((c & 0xF8) == 0xF0) {
            expected = 4;
        } else {
            // Invalid leading byte, replace with replacement char
            out += "\xEF\xBF\xBD"; // �
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
            // Replace invalid sequence with replacement char
            out += "\xEF\xBF\xBD"; // �
            ++i;
        }
    }
    
    return out;
}

// ============ Phone number utilities ============

// ============ Path utilities ============

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

bool create_parent_directory(const std::string& filepath) {
    // Find last '/' to get directory
    size_t pos = filepath.rfind('/');
    if (pos == std::string::npos) return true; // No directory component
    
    std::string dir = filepath.substr(0, pos);
    
    // Simple recursive mkdir
    std::string current;
    for (size_t i = 0; i < dir.size(); ++i) {
        current += dir[i];
        if (dir[i] == '/' || i == dir.size() - 1) {
            struct stat st;
            if (stat(current.c_str(), &st) != 0) {
                if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
                    return false;
                }
            }
        }
    }
    
    return true;
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
            if (peek == "script" || starts_with(peek, "script")) {
                in_script = true;
            } else if (peek == "/script") {
                in_script = false;
            } else if (peek == "style" || starts_with(peek, "style")) {
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

std::string sanitize_url(const std::string& url) {
    // First, strip HTML tags and normalize whitespace
    std::string stripped = strip_html_for_ai(url);
    
    // Then filter to keep only valid URL characters
    std::string result;
    result.reserve(stripped.size());
    
    for (char c : stripped) {
        // Keep valid URL characters (RFC 3986 unreserved characters + sub-delims + some others)
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
            (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || 
            c == '~' || c == ':' || c == '/' || c == '?' || c == '#' || 
            c == '[' || c == ']' || c == '@' || c == '!' || c == '$' || 
            c == '&' || c == '(' || c == ')' || c == '*' || c == '+' || 
            c == ',' || c == ';' || c == '=' || c == '%') {
            result += c;
        }
        // Skip other characters (including control chars, spaces, etc.)
    }
    
    return result;
}


} // namespace opencrank
