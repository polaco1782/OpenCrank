#include <opencrank/core/rate_limiter.hpp>
#include <opencrank/core/utils.hpp>
#include <algorithm>

namespace opencrank {

// ============ TokenBucketLimiter ============

TokenBucketLimiter::TokenBucketLimiter(int max_tokens, int refill_rate_per_second)
    : max_tokens_(max_tokens)
    , refill_rate_(refill_rate_per_second)
    , tokens_(static_cast<double>(max_tokens))
    , last_refill_ms_(current_timestamp_ms()) {}

void TokenBucketLimiter::refill() {
    int64_t now = current_timestamp_ms();
    int64_t elapsed = now - last_refill_ms_;
    
    if (elapsed > 0) {
        double tokens_to_add = (elapsed / 1000.0) * refill_rate_;
        tokens_ = std::min(static_cast<double>(max_tokens_), tokens_ + tokens_to_add);
        last_refill_ms_ = now;
    }
}

RateLimitResult TokenBucketLimiter::try_acquire() {
    refill();
    
    if (tokens_ >= 1.0) {
        tokens_ -= 1.0;
        return RateLimitResult::allow(static_cast<int>(tokens_), max_tokens_);
    }
    
    // Calculate time until next token
    double tokens_needed = 1.0 - tokens_;
    int64_t wait_ms = static_cast<int64_t>((tokens_needed / refill_rate_) * 1000);
    
    return RateLimitResult::deny(wait_ms, max_tokens_);
}

bool TokenBucketLimiter::would_allow() const {
    // Create a copy to check without modifying state
    TokenBucketLimiter copy = *this;
    copy.refill();
    return copy.tokens_ >= 1.0;
}

int TokenBucketLimiter::available_tokens() const {
    TokenBucketLimiter copy = *this;
    copy.refill();
    return static_cast<int>(copy.tokens_);
}

void TokenBucketLimiter::reset() {
    tokens_ = static_cast<double>(max_tokens_);
    last_refill_ms_ = current_timestamp_ms();
}

// ============ SlidingWindowLimiter ============

SlidingWindowLimiter::SlidingWindowLimiter(int max_requests, int window_seconds)
    : max_requests_(max_requests)
    , window_ms_(window_seconds * 1000) {}

void SlidingWindowLimiter::cleanup() {
    int64_t now = current_timestamp_ms();
    int64_t cutoff = now - window_ms_;
    
    while (!timestamps_.empty() && timestamps_.front() < cutoff) {
        timestamps_.pop_front();
    }
}

RateLimitResult SlidingWindowLimiter::try_acquire() {
    cleanup();
    
    int current = static_cast<int>(timestamps_.size());
    
    if (current < max_requests_) {
        timestamps_.push_back(current_timestamp_ms());
        return RateLimitResult::allow(max_requests_ - current - 1, max_requests_);
    }
    
    // Calculate time until oldest entry expires
    int64_t oldest = timestamps_.front();
    int64_t now = current_timestamp_ms();
    int64_t wait_ms = (oldest + window_ms_) - now;
    
    return RateLimitResult::deny(std::max(wait_ms, static_cast<int64_t>(1)), max_requests_);
}

int SlidingWindowLimiter::current_count() const {
    SlidingWindowLimiter copy = *this;
    copy.cleanup();
    return static_cast<int>(copy.timestamps_.size());
}

void SlidingWindowLimiter::reset() {
    timestamps_.clear();
}

// ============ KeyedRateLimiter ============

KeyedRateLimiter::KeyedRateLimiter(LimiterType type, int limit, int window_or_rate)
    : type_(type)
    , limit_(limit)
    , window_or_rate_(window_or_rate) {}

RateLimitResult KeyedRateLimiter::check(const std::string& key) {
    last_activity_[key] = current_timestamp();
    
    if (type_ == TOKEN_BUCKET) {
        auto [it, inserted] = token_limiters_.try_emplace(key, limit_, window_or_rate_);
        return it->second.try_acquire();
    } else {
        auto [it, inserted] = window_limiters_.try_emplace(key, limit_, window_or_rate_);
        return it->second.try_acquire();
    }
}

void KeyedRateLimiter::reset(const std::string& key) {
    if (type_ == TOKEN_BUCKET) {
        token_limiters_.erase(key);
    } else {
        window_limiters_.erase(key);
    }
    last_activity_.erase(key);
}

void KeyedRateLimiter::reset_all() {
    token_limiters_.clear();
    window_limiters_.clear();
    last_activity_.clear();
}

size_t KeyedRateLimiter::cleanup(int64_t max_age_seconds) {
    int64_t now = current_timestamp();
    size_t removed = 0;
    
    std::vector<std::string> to_remove;
    for (std::map<std::string, int64_t>::iterator it = last_activity_.begin();
         it != last_activity_.end(); ++it) {
        if (now - it->second > max_age_seconds) {
            to_remove.push_back(it->first);
        }
    }
    
    for (size_t i = 0; i < to_remove.size(); ++i) {
        reset(to_remove[i]);
        ++removed;
    }
    
    return removed;
}

size_t KeyedRateLimiter::key_count() const {
    if (type_ == TOKEN_BUCKET) {
        return token_limiters_.size();
    } else {
        return window_limiters_.size();
    }
}

// ============ TypingIndicator ============

TypingIndicator::TypingIndicator()
    : interval_ms_(5000) {}

void TypingIndicator::start_typing(const std::string& chat_id) {
    is_typing_[chat_id] = true;
}

void TypingIndicator::stop_typing(const std::string& chat_id) {
    is_typing_[chat_id] = false;
}

bool TypingIndicator::should_send_typing(const std::string& chat_id) {
    // Check if typing is active
    std::map<std::string, bool>::iterator typing_it = is_typing_.find(chat_id);
    if (typing_it == is_typing_.end() || !typing_it->second) {
        return false;
    }
    
    // Check if enough time has passed since last send
    int64_t now = current_timestamp_ms();
    std::map<std::string, int64_t>::iterator last_it = last_typing_.find(chat_id);
    
    if (last_it == last_typing_.end() || now - last_it->second >= interval_ms_) {
        last_typing_[chat_id] = now;
        return true;
    }
    
    return false;
}

std::vector<std::string> TypingIndicator::active_chats() const {
    std::vector<std::string> chats;
    for (std::map<std::string, bool>::const_iterator it = is_typing_.begin();
         it != is_typing_.end(); ++it) {
        if (it->second) {
            chats.push_back(it->first);
        }
    }
    return chats;
}

// ============ HeartbeatManager ============

HeartbeatManager::HeartbeatManager() {}

void HeartbeatManager::register_target(const std::string& target_id, int interval_seconds) {
    HeartbeatTarget target;
    target.interval_seconds = interval_seconds;
    target.last_sent = 0;
    target.last_received = current_timestamp();
    targets_[target_id] = target;
}

void HeartbeatManager::unregister_target(const std::string& target_id) {
    targets_.erase(target_id);
}

std::vector<std::string> HeartbeatManager::targets_due() const {
    std::vector<std::string> due;
    int64_t now = current_timestamp();
    
    for (std::map<std::string, HeartbeatTarget>::const_iterator it = targets_.begin();
         it != targets_.end(); ++it) {
        if (now - it->second.last_sent >= it->second.interval_seconds) {
            due.push_back(it->first);
        }
    }
    
    return due;
}

void HeartbeatManager::mark_sent(const std::string& target_id) {
    std::map<std::string, HeartbeatTarget>::iterator it = targets_.find(target_id);
    if (it != targets_.end()) {
        it->second.last_sent = current_timestamp();
    }
}

void HeartbeatManager::mark_received(const std::string& target_id) {
    std::map<std::string, HeartbeatTarget>::iterator it = targets_.find(target_id);
    if (it != targets_.end()) {
        it->second.last_received = current_timestamp();
    }
}

bool HeartbeatManager::is_healthy(const std::string& target_id) const {
    std::map<std::string, HeartbeatTarget>::const_iterator it = targets_.find(target_id);
    if (it == targets_.end()) return false;
    
    int64_t now = current_timestamp();
    int64_t max_age = it->second.interval_seconds * 2;  // 2x interval tolerance
    
    return (now - it->second.last_received) <= max_age;
}

std::vector<std::string> HeartbeatManager::unhealthy_targets() const {
    std::vector<std::string> unhealthy;
    
    for (std::map<std::string, HeartbeatTarget>::const_iterator it = targets_.begin();
         it != targets_.end(); ++it) {
        if (!is_healthy(it->first)) {
            unhealthy.push_back(it->first);
        }
    }
    
    return unhealthy;
}

// ============ MessageDebouncer ============

MessageDebouncer::MessageDebouncer(int window_seconds)
    : window_seconds_(window_seconds) {}

bool MessageDebouncer::should_process(const std::string& message_id) {
    cleanup();
    
    if (seen_messages_.find(message_id) != seen_messages_.end()) {
        return false;  // Duplicate
    }
    
    seen_messages_[message_id] = current_timestamp();
    return true;
}

void MessageDebouncer::cleanup() {
    int64_t now = current_timestamp();
    int64_t cutoff = now - window_seconds_;
    
    std::vector<std::string> to_remove;
    for (std::map<std::string, int64_t>::iterator it = seen_messages_.begin();
         it != seen_messages_.end(); ++it) {
        if (it->second < cutoff) {
            to_remove.push_back(it->first);
        }
    }
    
    for (size_t i = 0; i < to_remove.size(); ++i) {
        seen_messages_.erase(to_remove[i]);
    }
}

// ============ Throttler ============

Throttler::Throttler(int min_interval_ms)
    : min_interval_ms_(min_interval_ms)
    , last_action_ms_(0) {}

bool Throttler::should_proceed() {
    int64_t now = current_timestamp_ms();
    if (now - last_action_ms_ >= min_interval_ms_) {
        last_action_ms_ = now;
        return true;
    }
    return false;
}

void Throttler::wait() {
    int64_t now = current_timestamp_ms();
    int64_t wait = min_interval_ms_ - (now - last_action_ms_);
    
    if (wait > 0) {
        sleep_ms(static_cast<int>(wait));
    }
    
    last_action_ms_ = current_timestamp_ms();
}

void Throttler::reset() {
    last_action_ms_ = 0;
}

} // namespace opencrank
