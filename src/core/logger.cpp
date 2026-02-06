#include <opencrank/core/logger.hpp>

namespace opencrank {

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

void Logger::debug(const char* fmt, ...) {
    if (level_ > LogLevel::DEBUG) return;
    va_list args;
    va_start(args, fmt);
    log_impl("DEBUG", fmt, args);
    va_end(args);
}

void Logger::info(const char* fmt, ...) {
    if (level_ > LogLevel::INFO) return;
    va_list args;
    va_start(args, fmt);
    log_impl("INFO", fmt, args);
    va_end(args);
}

void Logger::warn(const char* fmt, ...) {
    if (level_ > LogLevel::WARN) return;
    va_list args;
    va_start(args, fmt);
    log_impl("WARN", fmt, args);
    va_end(args);
}

void Logger::error(const char* fmt, ...) {
    if (level_ > LogLevel::ERROR) return;
    va_list args;
    va_start(args, fmt);
    log_impl("ERROR", fmt, args);
    va_end(args);
}

Logger::Logger() : level_(LogLevel::INFO) {}

void Logger::log_impl(const char* level_str, const char* fmt, va_list args) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);
    
    fprintf(stderr, "[%s] [%s] ", timestamp, level_str);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    fflush(stderr);
}

} // namespace opencrank
