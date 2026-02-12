#include <opencrank/core/logger.hpp>

namespace opencrank {

static const char* get_color_code(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "\033[34m"; // Blue
        case LogLevel::INFO: return "\033[32m";  // Green
        case LogLevel::WARN: return "\033[33m";  // Yellow
        case LogLevel::ERROR: return "\033[31m"; // Red
        default: return "\033[0m"; // Reset
    }
}

static const char* get_function_color() {
    return "\033[36m"; // Cyan for class::function
}

static const char* get_location_color() {
    return "\033[33m"; // Yellow for file:line
}

static const char* get_level_str(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARN: return "WARN";
        case LogLevel::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

static std::pair<std::string, std::string> extract_class_and_function(const char* pretty_function) {
    std::string pf = pretty_function;
    
    // DEBUG: Print the pretty function
    // fprintf(stderr, "DEBUG: pretty_function='%s'\n", pf.c_str());
    
    // Find the opening parenthesis
    size_t paren_pos = pf.find('(');
    if (paren_pos == std::string::npos) {
        return {"", ""}; // Malformed
    }
    
    // Extract function signature up to parameters
    std::string signature = pf.substr(0, paren_pos);
    
    // Find the last "::" in the signature
    size_t last_colon = signature.rfind("::");
    if (last_colon == std::string::npos) {
        // No class, just function name - find last space
        size_t space_pos = signature.rfind(' ');
        std::string func_name = (space_pos != std::string::npos) ? signature.substr(space_pos + 1) : signature;
        return {"", func_name};
    }
    
    // Extract function name (after last ::)
    std::string func_name = signature.substr(last_colon + 2);
    
    // Extract class name (everything before last ::, after last space before that)
    std::string before_last_colon = signature.substr(0, last_colon);
    size_t space_pos = before_last_colon.rfind(' ');
    std::string class_name;
    if (space_pos != std::string::npos) {
        class_name = before_last_colon.substr(space_pos + 1);
    } else {
        class_name = before_last_colon;
    }
    
    // Remove any template parameters from class name
    size_t template_pos = class_name.find('<');
    if (template_pos != std::string::npos) {
        class_name = class_name.substr(0, template_pos);
    }
   
    // Remove leading "*" if present (for pointer types or similar)
    if (!class_name.empty() && class_name[0] == '*') {
        class_name = class_name.substr(1);
    }

    // Remove "opencrank::" namespace prefix from class name
    if (class_name.find("opencrank::") == 0) {
        class_name = class_name.substr(11);
    }
    
    // DEBUG: Print extracted values
    // fprintf(stderr, "DEBUG: class='%s', func='%s'\n", class_name.c_str(), func_name.c_str());
    
    return {class_name, func_name};
}

// Force visibility for the singleton across shared library boundaries
#ifdef __GNUC__
__attribute__((visibility("default")))
#endif
Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::set_level(LogLevel level) { level_ = level; }

LogLevel Logger::level() const { return level_; }

void Logger::debug(const char* file, int line, const char* func, const char* fmt, ...) {
    if (level_ > LogLevel::DEBUG) return;
    va_list args;
    va_start(args, fmt);
    log_impl(LogLevel::DEBUG, file, line, func, fmt, args);
    va_end(args);
}

void Logger::info(const char* file, int line, const char* func, const char* fmt, ...) {
    if (level_ > LogLevel::INFO) return;
    va_list args;
    va_start(args, fmt);
    log_impl(LogLevel::INFO, file, line, func, fmt, args);
    va_end(args);
}

void Logger::warn(const char* file, int line, const char* func, const char* fmt, ...) {
    if (level_ > LogLevel::WARN) return;
    va_list args;
    va_start(args, fmt);
    log_impl(LogLevel::WARN, file, line, func, fmt, args);
    va_end(args);
}

void Logger::error(const char* file, int line, const char* func, const char* fmt, ...) {
    if (level_ > LogLevel::ERROR) return;
    va_list args;
    va_start(args, fmt);
    log_impl(LogLevel::ERROR, file, line, func, fmt, args);
    va_end(args);
}

Logger::Logger() : level_(LogLevel::INFO) {}

void Logger::log_impl(LogLevel level, const char* file, int line, const char* func, const char* fmt, va_list args) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);
    
    const char* color = get_color_code(level);
    const char* level_str = get_level_str(level);
    const char* func_color = get_function_color();
    const char* location_color = get_location_color();
    auto [class_name, func_name] = extract_class_and_function(func);

    if(level_ == LogLevel::DEBUG) {
        if (!class_name.empty()) {
            fprintf(stderr, "[%s] %s[%s]\033[0m %s(%s::%s)\033[0m at %s%s:%d\033[0m ", 
                    timestamp, color, level_str, func_color, class_name.c_str(), func_name.c_str(), 
                    location_color, file, line);
        } else {
            fprintf(stderr, "[%s] %s[%s]\033[0m %s(%s)\033[0m at %s%s:%d\033[0m ", 
                    timestamp, color, level_str, func_color, func_name.c_str(), 
                    location_color, file, line);
        }
    } else {
        fprintf(stderr, "[%s] %s[%s]\033[0m ", timestamp, color, level_str);
    }
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    fflush(stderr);
}

} // namespace opencrank
