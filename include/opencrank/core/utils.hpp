#ifndef opencrank_CORE_UTILS_HPP
#define opencrank_CORE_UTILS_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <ctime>

namespace opencrank {

// ============ Math utilities ============

// Clamp a value between min and max
template<typename T>
T clamp(T value, T min_val, T max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

// ============ Time utilities ============

// Sleep for the specified number of milliseconds
void sleep_ms(int milliseconds);

// Get current Unix timestamp in seconds
int64_t current_timestamp();

// Get current Unix timestamp in milliseconds
int64_t current_timestamp_ms();

// Format timestamp as ISO 8601 string (YYYY-MM-DDTHH:MM:SSZ)
std::string format_timestamp(int64_t timestamp);

// ============ String utilities ============

// Trim whitespace from both ends of a string
std::string trim(const std::string& s);

// Trim whitespace from left side
std::string ltrim(const std::string& s);

// Trim whitespace from right side
std::string rtrim(const std::string& s);

// Convert string to lowercase
std::string to_lower(const std::string& s);

// Check if string starts with prefix
bool starts_with(const std::string& s, const std::string& prefix);

// Split string by delimiter
std::vector<std::string> split(const std::string& s, char delimiter);

// Split string by string delimiter
std::vector<std::string> split(const std::string& s, const std::string& delimiter);

// Join strings with delimiter
std::string join(const std::vector<std::string>& parts, const std::string& delimiter);

// Truncate string safely (UTF-8 aware, doesn't break multi-byte chars)
std::string truncate_safe(const std::string& s, size_t max_len);

// Sanitize a string for safe JSON serialization
// Replaces invalid UTF-8 sequences and problematic control characters
std::string sanitize_utf8(const std::string& s);

// ============ Phone number utilities ============

// ============ Path utilities ============

// Normalize path (resolve . and ..)
std::string normalize_path(const std::string& path);

// Join path components
std::string join_path(const std::string& a, const std::string& b);

// Create parent directory for a file path (recursive)
bool create_parent_directory(const std::string& filepath);

// ============ HTML utilities ============

// Strip HTML tags for AI consumption.
// Removes script/style blocks, decodes common entities, normalizes whitespace.
// Preserves <a> and <img> tags (with their attributes) for link/image context.
std::string strip_html_for_ai(const std::string& html);

// Normalize whitespace: collapse runs of whitespace to single space, trim.
std::string normalize_whitespace(const std::string& s);

// Sanitize URL by removing HTML tags and invalid characters
std::string sanitize_url(const std::string& url);

// ============ UUID utilities ============

// Generate a random UUID v4
std::string generate_uuid();

// ============ Hashing utilities ============

} // namespace opencrank

#endif // opencrank_CORE_UTILS_HPP
