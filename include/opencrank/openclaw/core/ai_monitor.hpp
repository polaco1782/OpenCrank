/*
 * OpenCrank C++11 - AI Process Monitor
 * 
 * Monitors AI processing activity with heartbeat tracking and hang detection.
 * Automatically sends typing indicators to channels while AI is working.
 * 
 * Features:
 * - Thread-safe heartbeat tracking for active AI sessions
 * - Hang detection with configurable timeout
 * - Automatic typing indicator dispatch to channels
 * - Periodic health check in dedicated monitoring thread
 */
#ifndef OPENCRANK_CORE_AI_MONITOR_HPP
#define OPENCRANK_CORE_AI_MONITOR_HPP

#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>

namespace opencrank {

// Forward declarations
class ChannelPlugin;

// ============================================================================
// AI Session State
// ============================================================================

struct AISessionState {
    std::string channel_id;     // Channel for sending typing indicator
    std::string chat_id;        // Chat/user ID to send typing indicator to
    std::chrono::steady_clock::time_point started_at;
    std::chrono::steady_clock::time_point last_heartbeat;
    int heartbeat_count;
    bool is_hung;
    
    AISessionState()
        : started_at(std::chrono::steady_clock::now())
        , last_heartbeat(std::chrono::steady_clock::now())
        , heartbeat_count(0)
        , is_hung(false)
    {}
};

// ============================================================================
// AI Process Monitor
// ============================================================================

class AIProcessMonitor {
public:
    // Configuration
    struct Config {
        int hang_timeout_seconds;      // Seconds without heartbeat = hung
        int typing_interval_seconds;   // How often to send typing indicator
        int check_interval_ms;         // Monitor thread check interval
        
        Config()
            : hang_timeout_seconds(60)
            , typing_interval_seconds(5)
            , check_interval_ms(1000)
        {}
    };
    
    AIProcessMonitor();
    ~AIProcessMonitor();
    
    // Lifecycle
    void start();
    void stop();
    bool is_running() const { return running_.load(); }
    
    // Configuration
    void set_config(const Config& config) { config_ = config; }
    const Config& get_config() const { return config_; }
    
    // Session tracking
    void start_session(const std::string& session_id, 
                      const std::string& channel_id,
                      const std::string& chat_id);
    void heartbeat(const std::string& session_id);
    void end_session(const std::string& session_id);
    
    // Hung session callback
    using HungSessionCallback = std::function<void(const std::string& session_id, 
                                                    int elapsed_seconds)>;
    void set_hung_callback(HungSessionCallback cb) { hung_callback_ = cb; }
    
    // Statistics
    struct Stats {
        int active_sessions;
        int total_sessions_started;
        int total_hung_detected;
        int total_typing_indicators_sent;
    };
    Stats get_stats() const;
    
private:
    // Configuration
    Config config_;
    
    // State
    std::atomic<bool> running_;
    std::thread monitor_thread_;
    
    // Session tracking
    mutable std::mutex sessions_mutex_;
    std::map<std::string, AISessionState> active_sessions_;
    
    // Statistics
    std::atomic<int> total_sessions_started_;
    std::atomic<int> total_hung_detected_;
    std::atomic<int> total_typing_indicators_sent_;
    
    // Callbacks
    HungSessionCallback hung_callback_;
    
    // Monitoring thread
    void monitor_loop();
    
    // Check a single session
    void check_session(const std::string& session_id, AISessionState& state);
    
    // Send typing indicator to channel
    void send_typing_indicator(const std::string& channel_id, 
                               const std::string& chat_id);
};

} // namespace opencrank

#endif // OPENCRANK_CORE_AI_MONITOR_HPP
