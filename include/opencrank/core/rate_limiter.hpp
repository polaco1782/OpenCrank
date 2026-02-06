#ifndef opencrank_CORE_RATE_LIMITER_HPP
#define opencrank_CORE_RATE_LIMITER_HPP

#include <string>
#include <vector>
#include <map>
#include <deque>
#include <cstdint>

namespace opencrank {

// Rate limit result
struct RateLimitResult {
    bool allowed;
    int64_t retry_after_ms;    // Milliseconds until next allowed request
    int remaining;             // Remaining requests in window
    int limit;                 // Total limit per window
    
    RateLimitResult() 
        : allowed(true)
        , retry_after_ms(0)
        , remaining(0)
        , limit(0) {}
    
    static RateLimitResult allow(int remaining, int limit) {
        RateLimitResult r;
        r.allowed = true;
        r.remaining = remaining;
        r.limit = limit;
        return r;
    }
    
    static RateLimitResult deny(int64_t retry_after, int limit) {
        RateLimitResult r;
        r.allowed = false;
        r.retry_after_ms = retry_after;
        r.limit = limit;
        r.remaining = 0;
        return r;
    }
};

// Token bucket rate limiter
class TokenBucketLimiter {
public:
    TokenBucketLimiter(int max_tokens, int refill_rate_per_second);
    
    // Try to consume a token
    RateLimitResult try_acquire();
    
    // Check if a token would be available (without consuming)
    bool would_allow() const;
    
    // Get current token count
    int available_tokens() const;
    
    // Reset to full capacity
    void reset();
    
private:
    void refill();
    
    int max_tokens_;
    int refill_rate_;
    double tokens_;
    int64_t last_refill_ms_;
};

// Sliding window rate limiter (more precise but uses more memory)
class SlidingWindowLimiter {
public:
    SlidingWindowLimiter(int max_requests, int window_seconds);
    
    // Try to record a request
    RateLimitResult try_acquire();
    
    // Check current count without recording
    int current_count() const;
    
    // Reset the window
    void reset();
    
private:
    void cleanup();
    
    int max_requests_;
    int window_ms_;
    std::deque<int64_t> timestamps_;
};

// Per-key rate limiter (e.g., per user, per channel)
class KeyedRateLimiter {
public:
    enum LimiterType {
        TOKEN_BUCKET,
        SLIDING_WINDOW
    };
    
    KeyedRateLimiter(LimiterType type, int limit, int window_or_rate);
    
    // Check rate limit for a key
    RateLimitResult check(const std::string& key);
    
    // Reset a specific key
    void reset(const std::string& key);
    
    // Reset all keys
    void reset_all();
    
    // Clean up inactive keys (older than max_age_seconds)
    size_t cleanup(int64_t max_age_seconds);
    
    // Get key count
    size_t key_count() const;

private:
    LimiterType type_;
    int limit_;
    int window_or_rate_;
    
    // For token bucket
    std::map<std::string, TokenBucketLimiter> token_limiters_;
    
    // For sliding window
    std::map<std::string, SlidingWindowLimiter> window_limiters_;
    
    // Track last activity for cleanup
    std::map<std::string, int64_t> last_activity_;
};

// Typing indicator manager
class TypingIndicator {
public:
    TypingIndicator();
    
    // Start typing indicator for a chat
    void start_typing(const std::string& chat_id);
    
    // Stop typing indicator for a chat
    void stop_typing(const std::string& chat_id);
    
    // Check if we should send typing indicator (with throttling)
    bool should_send_typing(const std::string& chat_id);
    
    // Get chats with active typing
    std::vector<std::string> active_chats() const;
    
    // Typing indicator interval in milliseconds (default 5000)
    void set_interval(int ms) { interval_ms_ = ms; }
    int interval() const { return interval_ms_; }

private:
    int interval_ms_;
    std::map<std::string, int64_t> last_typing_;  // chat_id -> last send time
    std::map<std::string, bool> is_typing_;       // chat_id -> is typing
};

// Heartbeat/keep-alive manager
class HeartbeatManager {
public:
    HeartbeatManager();
    
    // Register a heartbeat target
    void register_target(const std::string& target_id, int interval_seconds);
    
    // Unregister a target
    void unregister_target(const std::string& target_id);
    
    // Check which targets need heartbeat
    std::vector<std::string> targets_due() const;
    
    // Mark heartbeat sent for a target
    void mark_sent(const std::string& target_id);
    
    // Mark heartbeat received for a target
    void mark_received(const std::string& target_id);
    
    // Check if a target is healthy (received heartbeat within 2x interval)
    bool is_healthy(const std::string& target_id) const;
    
    // Get unhealthy targets
    std::vector<std::string> unhealthy_targets() const;

private:
    struct HeartbeatTarget {
        int interval_seconds;
        int64_t last_sent;
        int64_t last_received;
    };
    
    std::map<std::string, HeartbeatTarget> targets_;
};

// Message debouncer (prevents duplicate message handling)
class MessageDebouncer {
public:
    MessageDebouncer(int window_seconds = 5);
    
    // Check if message should be processed (returns false if duplicate)
    bool should_process(const std::string& message_id);
    
    // Clean up old entries
    void cleanup();
    
    // Set dedup window
    void set_window(int seconds) { window_seconds_ = seconds; }

private:
    int window_seconds_;
    std::map<std::string, int64_t> seen_messages_;
};

// Throttler for general-purpose rate limiting
class Throttler {
public:
    // Throttle calls to minimum interval
    Throttler(int min_interval_ms);
    
    // Check if action should proceed
    bool should_proceed();
    
    // Wait until action can proceed (blocks)
    void wait();
    
    // Reset the throttle
    void reset();

private:
    int min_interval_ms_;
    int64_t last_action_ms_;
};

} // namespace opencrank

#endif // opencrank_CORE_RATE_LIMITER_HPP
