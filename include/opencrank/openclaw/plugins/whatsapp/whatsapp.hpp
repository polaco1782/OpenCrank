#ifndef OPENCRANK_PLUGINS_WHATSAPP_HPP
#define OPENCRANK_PLUGINS_WHATSAPP_HPP

#include <opencrank/core/channel.hpp>
#include <opencrank/core/http_client.hpp>
#include <opencrank/core/logger.hpp>
#include <string>
#include <sstream>
#include <ctime>
#include <map>

namespace opencrank {

/*
 * WhatsApp Channel Plugin
 * 
 * This plugin supports two modes:
 * 
 * 1. WhatsApp Business API (Cloud API)
 *    - Requires Meta Business account and phone number registration
 *    - Set WHATSAPP_PHONE_NUMBER_ID and WHATSAPP_ACCESS_TOKEN
 * 
 * 2. Local Bridge Mode (e.g., whatsapp-web.js bridge)
 *    - Runs a local server that handles WhatsApp Web connection
 *    - Set WHATSAPP_BRIDGE_URL to the bridge endpoint
 *    - Bridge handles QR code authentication
 * 
 * The plugin auto-detects which mode based on available config.
 */
class WhatsAppChannel : public ChannelPlugin {
public:
    enum Mode {
        MODE_NONE,
        MODE_CLOUD_API,    // Meta Cloud API
        MODE_BRIDGE        // Local bridge (whatsapp-web.js, etc.)
    };
    
    WhatsAppChannel();
    
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
    
    // Poll for updates
    void poll();
    
    // Get mode for external inspection
    Mode mode() const;

private:
    std::string phone_number_id_;
    std::string access_token_;
    std::string bridge_url_;
    std::string api_base_;
    HttpClient http_;
    ChannelStatus status_;
    Mode mode_;
    int poll_interval_;
    int64_t last_poll_time_;
    
    std::string normalize_phone(const std::string& phone);
    
    // Cloud API methods
    bool verify_cloud_api();
    SendResult send_cloud_api(const std::string& to, const std::string& text,
                              const std::string& reply_to = "");
    
    // Bridge mode methods
    bool verify_bridge();
    SendResult send_bridge(const std::string& to, const std::string& text,
                           const std::string& reply_to = "");
    void poll_bridge();
    void process_bridge_message(const Json& msg);
};

} // namespace opencrank

#endif // OPENCRANK_CHANNELS_WHATSAPP_HPP
