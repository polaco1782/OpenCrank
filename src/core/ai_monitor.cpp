/*
 * OpenCrank C++11 - AI Process Monitor Implementation
 */
#include <opencrank/core/ai_monitor.hpp>
#include <opencrank/core/application.hpp>
#include <opencrank/core/channel.hpp>
#include <opencrank/core/logger.hpp>
#include <opencrank/core/message_handler.hpp>

namespace opencrank {

// ============================================================================
// Constructor / Destructor
// ============================================================================

AIProcessMonitor::AIProcessMonitor()
    : running_(false)
    , total_sessions_started_(0)
    , total_hung_detected_(0)
    , total_typing_indicators_sent_(0)
{
}

AIProcessMonitor::~AIProcessMonitor() {
    stop();
}

// ============================================================================
// Lifecycle
// ============================================================================

void AIProcessMonitor::start() {
    if (running_.load()) {
        LOG_WARN("[AIProcessMonitor] already running");
        return;
    }
    
    running_.store(true);
    monitor_thread_ = std::thread(&AIProcessMonitor::monitor_loop, this);
    
    LOG_INFO("[AIProcessMonitor] started (hang_timeout=%ds, typing_interval=%ds)",
             config_.hang_timeout_seconds, config_.typing_interval_seconds);
}

void AIProcessMonitor::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    
    LOG_INFO("[AIProcessMonitor] stopped");
}

// ============================================================================
// Session Tracking
// ============================================================================

void AIProcessMonitor::start_session(const std::string& session_id,
                                     const std::string& channel_id,
                                     const std::string& chat_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    AISessionState state;
    state.channel_id = channel_id;
    state.chat_id = chat_id;
    
    active_sessions_[session_id] = state;
    total_sessions_started_.fetch_add(1);
    
    LOG_DEBUG("[AIProcessMonitor] session started [%s] -> %s:%s", 
              session_id.c_str(), channel_id.c_str(), chat_id.c_str());
}

void AIProcessMonitor::heartbeat(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = active_sessions_.find(session_id);
    if (it != active_sessions_.end()) {
        it->second.last_heartbeat = std::chrono::steady_clock::now();
        it->second.heartbeat_count++;
        it->second.is_hung = false;
        
        LOG_DEBUG("[AIProcessMonitor] heartbeat [%s] count=%d", 
                  session_id.c_str(), it->second.heartbeat_count);
    }
}

void AIProcessMonitor::end_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = active_sessions_.find(session_id);
    if (it != active_sessions_.end()) {
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - it->second.started_at
        ).count();
        
        LOG_DEBUG("[AIProcessMonitor] session ended [%s] duration=%llds heartbeats=%d hung=%s",
                  session_id.c_str(), 
                  static_cast<long long>(duration),
                  it->second.heartbeat_count,
                  it->second.is_hung ? "yes" : "no");
        
        // Stop typing indicator
        auto& state = it->second;
        auto& app = Application::instance();
        auto& plugins = app.registry().plugins();
        for (size_t i = 0; i < plugins.size(); ++i) {
            plugins[i]->on_typing_indicator(state.channel_id, state.chat_id, false);
        }
        
        active_sessions_.erase(it);
    }
}

// ============================================================================
// Statistics
// ============================================================================

AIProcessMonitor::Stats AIProcessMonitor::get_stats() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    Stats stats;
    stats.active_sessions = static_cast<int>(active_sessions_.size());
    stats.total_sessions_started = total_sessions_started_.load();
    stats.total_hung_detected = total_hung_detected_.load();
    stats.total_typing_indicators_sent = total_typing_indicators_sent_.load();
    
    return stats;
}

// ============================================================================
// Monitoring Thread
// ============================================================================

void AIProcessMonitor::monitor_loop() {
    LOG_DEBUG("[AIProcessMonitor] monitor thread started");
    
    auto last_typing_time = std::chrono::steady_clock::now();
    
    while (running_.load()) {
        auto now = std::chrono::steady_clock::now();
        
        // Check all sessions
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            
            for (auto& entry : active_sessions_) {
                check_session(entry.first, entry.second);
            }
        }
        
        // Send typing indicators periodically
        auto elapsed_typing = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_typing_time
        ).count();
        
        if (elapsed_typing >= config_.typing_interval_seconds) {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            
            for (const auto& entry : active_sessions_) {
                const auto& state = entry.second;
                send_typing_indicator(state.channel_id, state.chat_id);
            }
            
            last_typing_time = now;
        }
        
        // Sleep
        std::this_thread::sleep_for(
            std::chrono::milliseconds(config_.check_interval_ms)
        );
    }
    
    LOG_DEBUG("[AIProcessMonitor] monitor thread exited");
}

void AIProcessMonitor::check_session(const std::string& session_id, 
                                     AISessionState& state) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - state.last_heartbeat
    ).count();
    
    // Check for hung session
    if (!state.is_hung && elapsed >= config_.hang_timeout_seconds) {
        state.is_hung = true;
        total_hung_detected_.fetch_add(1);
        
        LOG_WARN("[AIProcessMonitor] HUNG SESSION DETECTED [%s] - no heartbeat for %llds",
                 session_id.c_str(), static_cast<long long>(elapsed));
        
        // Broadcast critical notification about hung session
        std::string message = "AI session '" + session_id + "' has been detected as hung after " + 
                             std::to_string(static_cast<long long>(elapsed)) + " seconds of inactivity";
        broadcast_notification(message, "critical", "\xf0\x9f\x9a\xa8");  // 🚨
        
        // Invoke callback if set
        if (hung_callback_) {
            hung_callback_(session_id, static_cast<int>(elapsed));
        }
    }
}

void AIProcessMonitor::send_typing_indicator(const std::string& channel_id,
                                             const std::string& chat_id) {
    auto& app = Application::instance();
    auto* channel = app.registry().get_channel(channel_id);
    
    if (!channel) {
        LOG_DEBUG("[AIProcessMonitor] channel not found: %s", channel_id.c_str());
        return;
    }
    
    // Check if channel supports typing indicators
    auto caps = channel->capabilities();
    if (!caps.supports_typing) {
        LOG_DEBUG("[AIProcessMonitor] channel %s does not support typing", channel_id.c_str());
        return;
    }
    
    LOG_DEBUG("[AIProcessMonitor] requesting typing indicator for %s:%s",
              channel_id.c_str(), chat_id.c_str());
    
    // Cast to get access to send_typing_action
    // Note: This is a safe pattern since we checked capabilities
    auto send_result = channel->send_typing_action(chat_id);
    
    if (send_result.success) {
        total_typing_indicators_sent_.fetch_add(1);
        LOG_INFO("[AIProcessMonitor] typing indicator sent to %s:%s (total: %d)",
                  channel_id.c_str(), chat_id.c_str(), total_typing_indicators_sent_.load());
    } else {
        LOG_WARN("[AIProcessMonitor] failed to send typing indicator to %s:%s - %s",
                  channel_id.c_str(), chat_id.c_str(), send_result.error.c_str());
    }
    
    // Notify all plugins about typing indicator
    auto& plugins = app.registry().plugins();
    for (size_t i = 0; i < plugins.size(); ++i) {
        plugins[i]->on_typing_indicator(channel_id, chat_id, true);
    }
}

} // namespace opencrank
