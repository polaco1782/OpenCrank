#ifndef opencrank_CORE_LOGGER_HPP
#define opencrank_CORE_LOGGER_HPP

#include <string>
#include <utility>
#include <cstdio>
#include <ctime>
#include <cstdarg>

namespace opencrank {

// Visibility attribute for plugin-visible symbols
#ifdef __GNUC__
#  define LOGGER_API __attribute__((visibility("default")))
#else
#  define LOGGER_API
#endif

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3
};

class LOGGER_API Logger {
public:
    static Logger& instance();
    
    void set_level(LogLevel level);
    LogLevel level() const;
    
    void debug(const char* file, int line, const char* func, const char* fmt, ...);
    void info(const char* file, int line, const char* func, const char* fmt, ...);
    void warn(const char* file, int line, const char* func, const char* fmt, ...);
    void error(const char* file, int line, const char* func, const char* fmt, ...);

private:
    Logger();
    Logger(const Logger&);
    Logger& operator=(const Logger&);
    
    void log_impl(LogLevel level, const char* file, int line, const char* func, const char* fmt, va_list args);
    
    LogLevel level_;
};

// Convenience macros
#define LOG_DEBUG(...) opencrank::Logger::instance().debug(__FILE__, __LINE__, __PRETTY_FUNCTION__, __VA_ARGS__)
#define LOG_INFO(...)  opencrank::Logger::instance().info(__FILE__, __LINE__, __PRETTY_FUNCTION__, __VA_ARGS__)
#define LOG_WARN(...)  opencrank::Logger::instance().warn(__FILE__, __LINE__, __PRETTY_FUNCTION__, __VA_ARGS__)
#define LOG_ERROR(...) opencrank::Logger::instance().error(__FILE__, __LINE__, __PRETTY_FUNCTION__, __VA_ARGS__)

} // namespace opencrank

#endif // opencrank_CORE_LOGGER_HPP
