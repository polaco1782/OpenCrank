#ifndef opencrank_PLUGINS_TELEGRAM_HPP
#define opencrank_PLUGINS_TELEGRAM_HPP

#include <opencrank/core/channel.hpp>
#include <opencrank/core/http_client.hpp>
#include <opencrank/core/logger.hpp>
#include <string>
#include <sstream>
#include <ctime>
#include <thread>
#include <atomic>
#include <mutex>

namespace opencrank {

// Telegram Bot API channel plugin
class TelegramChannel : public ChannelPlugin {
public:
    TelegramChannel();
    
    // Plugin interface
    const char* name() const;
    const char* version() const;
    const char* description() const;
    
    // Channel interface
    const char* channel_id() const;
    ChannelCapabilities capabilities() const;
    
    bool init(const Config& cfg);
    void shutdown();
    bool start();
    bool stop();
    ChannelStatus status() const;
    
    // Send a message
    SendResult send_message(const std::string& to, const std::string& text);
    SendResult send_message(const std::string& to, const std::string& text,
                            const std::string& reply_to);
    
    // Send typing action
    SendResult send_typing_action(const std::string& to);
    
    // Poll for updates (call this regularly)
    void poll();

private:
    std::string bot_token_;
    std::string api_base_;
    int64_t bot_id_;
    std::string bot_username_;
    HttpClient http_;        // For polling
    HttpClient http_send_;   // For sending messages (separate to avoid blocking)
    ChannelStatus status_;
    int64_t last_update_id_;
    int poll_timeout_;
    
    // Polling thread
    std::thread poll_thread_;
    std::atomic<bool> should_stop_polling_;
    
    void polling_loop();
    SendResult send_message_impl(const std::string& to, const std::string& text, int64_t reply_to);
    void process_update(const Json& update);
};

} // namespace opencrank

#endif // opencrank_CHANNELS_TELEGRAM_HPP
