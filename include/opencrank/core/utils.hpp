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

// Clamp an integer value
int clamp_int(int value, int min_val, int max_val);

// ============ Time utilities ============

// Sleep for the specified number of milliseconds
void sleep_ms(int milliseconds);

// Get current Unix timestamp in seconds
int64_t current_timestamp();

// Get current Unix timestamp in milliseconds
int64_t current_timestamp_ms();

// Format timestamp as ISO 8601 string (YYYY-MM-DDTHH:MM:SSZ)
std::string format_timestamp(int64_t timestamp);

// Format timestamp as date string (YYYY-MM-DD)
std::string format_date(int64_t timestamp);

// ============ String utilities ============

// Trim whitespace from both ends of a string
std::string trim(const std::string& s);

// Trim whitespace from left side
std::string ltrim(const std::string& s);

// Trim whitespace from right side
std::string rtrim(const std::string& s);

// Convert string to lowercase
std::string to_lower(const std::string& s);

// Convert string to uppercase
std::string to_upper(const std::string& s);

// Check if string starts with prefix
bool starts_with(const std::string& s, const std::string& prefix);

// Check if string ends with suffix
bool ends_with(const std::string& s, const std::string& suffix);

// Split string by delimiter
std::vector<std::string> split(const std::string& s, char delimiter);

// Split string by string delimiter
std::vector<std::string> split(const std::string& s, const std::string& delimiter);

// Join strings with delimiter
std::string join(const std::vector<std::string>& parts, const std::string& delimiter);

// Replace all occurrences of 'from' with 'to'
std::string replace_all(const std::string& s, const std::string& from, const std::string& to);

// Truncate string safely (UTF-8 aware, doesn't break multi-byte chars)
std::string truncate_safe(const std::string& s, size_t max_len);

// Sanitize a string for safe JSON serialization
// Replaces invalid UTF-8 sequences and problematic control characters
std::string sanitize_utf8(const std::string& s);

// ============ Phone number utilities ============

// Normalize phone number to E.164 format (+1234567890)
std::string normalize_e164(const std::string& number);

// Add whatsapp: prefix if not present
std::string with_whatsapp_prefix(const std::string& number);

// Remove whatsapp: prefix if present
std::string without_whatsapp_prefix(const std::string& number);

// Convert WhatsApp JID to E.164 (e.g., 1234567890@s.whatsapp.net -> +1234567890)
std::string jid_to_e164(const std::string& jid);

// Convert E.164 to WhatsApp JID
std::string e164_to_jid(const std::string& e164);

// ============ Path utilities ============

// Resolve ~ to home directory
std::string resolve_user_path(const std::string& path);

// Get home directory
std::string get_home_dir();

// Normalize path (resolve . and ..)
std::string normalize_path(const std::string& path);

// Join path components
std::string join_path(const std::string& a, const std::string& b);

// Get basename (filename) from path
std::string basename(const std::string& path);

// Get directory name from path
std::string dirname(const std::string& path);

// Check if path exists
bool path_exists(const std::string& path);

// Check if path is a directory
bool is_directory(const std::string& path);

// Create directory (and parents if needed)
bool mkdir_p(const std::string& path);

// Shorten path by replacing home with ~
std::string shorten_home_path(const std::string& path);

// ============ HTML utilities ============

// Strip HTML tags for AI consumption.
// Removes script/style blocks, decodes common entities, normalizes whitespace.
// Preserves <a> and <img> tags (with their attributes) for link/image context.
std::string strip_html_for_ai(const std::string& html);

// Normalize whitespace: collapse runs of whitespace to single space, trim.
std::string normalize_whitespace(const std::string& s);

// ============ UUID utilities ============

// Generate a random UUID v4
std::string generate_uuid();

// ============ Hashing utilities ============

// Compute SHA256 hash as hex string
std::string sha256_hex(const std::string& data);

// Compute MD5 hash as hex string
std::string md5_hex(const std::string& data);

} // namespace opencrank

#endif // opencrank_CORE_UTILS_HPP
