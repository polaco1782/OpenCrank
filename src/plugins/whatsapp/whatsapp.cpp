#include <opencrank/plugins/whatsapp/whatsapp.hpp>
#include <opencrank/core/loader.hpp>
#include <sstream>
#include <ctime>

namespace opencrank {

WhatsAppChannel::WhatsAppChannel() 
    : status_(ChannelStatus::STOPPED)
    , mode_(MODE_NONE)
    , poll_interval_(5)
    , last_poll_time_(0) {}

const char* WhatsAppChannel::name() const { return "whatsapp"; }
const char* WhatsAppChannel::version() const { return "1.0.0"; }
const char* WhatsAppChannel::description() const { return "WhatsApp messaging channel"; }
const char* WhatsAppChannel::channel_id() const { return "whatsapp"; }

ChannelCapabilities WhatsAppChannel::capabilities() const {
    ChannelCapabilities caps;
    caps.supports_groups = true;
    caps.supports_reactions = true;
    caps.supports_media = true;
    caps.supports_edit = false;
    caps.supports_delete = true;
    caps.supports_typing = true;
    return caps;
}

bool WhatsAppChannel::init(const Config& cfg) {
    phone_number_id_ = cfg.get_channel_string("whatsapp", "phone_number_id");
    access_token_ = cfg.get_channel_string("whatsapp", "access_token");
    bridge_url_ = cfg.get_channel_string("whatsapp", "bridge_url");
    
    if (!phone_number_id_.empty() && !access_token_.empty()) {
        mode_ = MODE_CLOUD_API;
        api_base_ = "https://graph.facebook.com/v18.0/" + phone_number_id_;
        LOG_INFO("WhatsApp: configured for Cloud API mode");
    } else if (!bridge_url_.empty()) {
        mode_ = MODE_BRIDGE;
        api_base_ = bridge_url_;
        LOG_INFO("WhatsApp: configured for Bridge mode at %s", bridge_url_.c_str());
    } else {
        LOG_WARN("WhatsApp: no valid configuration found in config.json");
        LOG_WARN("  Set whatsapp.phone_number_id + whatsapp.access_token for Cloud API");
        LOG_WARN("  Or set whatsapp.bridge_url for local bridge mode");
        return false;
    }
    
    poll_interval_ = static_cast<int>(cfg.get_int("whatsapp.poll_interval", 5));
    
    initialized_ = true;
    return true;
}

void WhatsAppChannel::shutdown() {
    stop();
    initialized_ = false;
    LOG_INFO("WhatsApp: shutdown");
}

bool WhatsAppChannel::start() {
    if (!initialized_) {
        LOG_ERROR("WhatsApp: cannot start - not initialized");
        return false;
    }
    
    status_ = ChannelStatus::STARTING;
    
    if (mode_ == MODE_CLOUD_API) {
        if (!verify_cloud_api()) {
            status_ = ChannelStatus::ERROR;
            return false;
        }
    } else if (mode_ == MODE_BRIDGE) {
        if (!verify_bridge()) {
            status_ = ChannelStatus::ERROR;
            return false;
        }
    }
    
    status_ = ChannelStatus::RUNNING;
    LOG_INFO("WhatsApp: started in %s mode", 
             mode_ == MODE_CLOUD_API ? "Cloud API" : "Bridge");
    return true;
}

bool WhatsAppChannel::stop() {
    if (status_ == ChannelStatus::STOPPED) return true;
    
    status_ = ChannelStatus::STOPPING;
    status_ = ChannelStatus::STOPPED;
    
    LOG_INFO("WhatsApp: stopped");
    return true;
}

ChannelStatus WhatsAppChannel::status() const { return status_; }

SendResult WhatsAppChannel::send_message(const std::string& to, const std::string& text) {
    if (mode_ == MODE_CLOUD_API) {
        return send_cloud_api(to, text);
    } else if (mode_ == MODE_BRIDGE) {
        return send_bridge(to, text);
    }
    return SendResult::fail("WhatsApp not configured");
}

SendResult WhatsAppChannel::send_message(const std::string& to, const std::string& text,
                                         const std::string& reply_to) {
    if (mode_ == MODE_CLOUD_API) {
        return send_cloud_api(to, text, reply_to);
    } else if (mode_ == MODE_BRIDGE) {
        return send_bridge(to, text, reply_to);
    }
    return SendResult::fail("WhatsApp not configured");
}

SendResult WhatsAppChannel::send_typing_action(const std::string& to) {
    if (mode_ == MODE_CLOUD_API) {
        // WhatsApp Cloud API doesn't have a typing indicator endpoint
        // It's a limitation of the official API
        LOG_DEBUG("WhatsApp Cloud API: typing indicator not supported");
        return SendResult::fail("Typing action not supported in Cloud API mode");
    } else if (mode_ == MODE_BRIDGE) {
        // Bridge mode might support typing via custom endpoint
        std::string phone = normalize_phone(to);
        
        Json params = Json::object();
        params["phone"] = phone;
        params["action"] = "typing";
        
        HttpResponse resp = http_.post_json(api_base_ + "/typing", params);
        if (!resp.ok()) {
            LOG_DEBUG("WhatsApp Bridge: typing action failed - %s", resp.error.c_str());
            return SendResult::fail("HTTP error: " + resp.error);
        }
        
        LOG_DEBUG("WhatsApp: sent typing action to %s", to.c_str());
        return SendResult::ok("");
    }
    return SendResult::fail("WhatsApp not configured");
}

void WhatsAppChannel::poll() {
    if (status_ != ChannelStatus::RUNNING) return;
    
    if (mode_ == MODE_BRIDGE) {
        poll_bridge();
    }
}

WhatsAppChannel::Mode WhatsAppChannel::mode() const { return mode_; }

std::string WhatsAppChannel::normalize_phone(const std::string& phone) {
    std::string result;
    for (size_t i = 0; i < phone.size(); ++i) {
        char c = phone[i];
        if (c >= '0' && c <= '9') {
            result += c;
        }
    }
    while (!result.empty() && result[0] == '0') {
        result.erase(0, 1);
    }
    return result;
}

bool WhatsAppChannel::verify_cloud_api() {
    std::map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + access_token_;
    
    HttpResponse resp = http_.get(api_base_ + "?fields=id,display_phone_number", headers);
    if (!resp.ok()) {
        LOG_ERROR("WhatsApp Cloud API: connection failed - %s", resp.error.c_str());
        return false;
    }
    
    Json result = resp.json();
    if (result.contains("error")) {
        LOG_ERROR("WhatsApp Cloud API: %s", result["error"].value("message", std::string("unknown error")).c_str());
        return false;
    }
    
    std::string display_number = result.value("display_phone_number", std::string(""));
    LOG_INFO("WhatsApp Cloud API: connected as %s", display_number.c_str());
    return true;
}

SendResult WhatsAppChannel::send_cloud_api(const std::string& to, const std::string& text,
                                           const std::string& reply_to) {
    std::string phone = normalize_phone(to);
    
    Json message = Json::object();
    message["messaging_product"] = "whatsapp";
    message["recipient_type"] = "individual";
    message["to"] = phone;
    message["type"] = "text";
    
    Json text_obj = Json::object();
    text_obj["preview_url"] = false;
    text_obj["body"] = text;
    message["text"] = text_obj;
    
    if (!reply_to.empty()) {
        Json context = Json::object();
        context["message_id"] = reply_to;
        message["context"] = context;
    }
    
    std::map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + access_token_;
    
    HttpResponse resp = http_.post_json(api_base_ + "/messages", message, headers);
    
    if (!resp.ok()) {
        return SendResult::fail("HTTP error: " + resp.error);
    }
    
    Json result = resp.json();
    if (result.contains("error")) {
        return SendResult::fail("API error: " + result["error"].value("message", std::string("unknown error")));
    }
    
    std::string msg_id;
    if (result.contains("messages") && result["messages"].is_array() && !result["messages"].empty()) {
        msg_id = result["messages"][0].value("id", std::string(""));
    }
    LOG_DEBUG("WhatsApp: sent message to %s (id=%s)", phone.c_str(), msg_id.c_str());
    return SendResult::ok(msg_id);
}

bool WhatsAppChannel::verify_bridge() {
    HttpResponse resp = http_.get(api_base_ + "/status");
    if (!resp.ok()) {
        LOG_ERROR("WhatsApp Bridge: connection failed - %s", resp.error.c_str());
        return false;
    }
    
    Json result = resp.json();
    if (!result.value("connected", false)) {
        LOG_WARN("WhatsApp Bridge: not connected to WhatsApp Web");
        LOG_WARN("WhatsApp Bridge: scan QR code at %s/qr", bridge_url_.c_str());
    } else {
        std::string phone = result.value("phone", std::string(""));
        LOG_INFO("WhatsApp Bridge: connected as %s", phone.c_str());
    }
    return true;
}

SendResult WhatsAppChannel::send_bridge(const std::string& to, const std::string& text,
                                        const std::string& reply_to) {
    Json message = Json::object();
    message["to"] = to;
    message["text"] = text;
    if (!reply_to.empty()) {
        message["reply_to"] = reply_to;
    }
    
    HttpResponse resp = http_.post_json(api_base_ + "/send", message);
    
    if (!resp.ok()) {
        return SendResult::fail("HTTP error: " + resp.error);
    }
    
    Json result = resp.json();
    if (!result.value("success", false)) {
        return SendResult::fail("Bridge error: " + result.value("error", std::string("unknown error")));
    }
    
    std::string msg_id = result.value("message_id", std::string(""));
    LOG_DEBUG("WhatsApp: sent message to %s via bridge (id=%s)", to.c_str(), msg_id.c_str());
    return SendResult::ok(msg_id);
}

void WhatsAppChannel::poll_bridge() {
    time_t now = time(NULL);
    if (now - last_poll_time_ < poll_interval_) return;
    last_poll_time_ = now;
    
    HttpResponse resp = http_.get(api_base_ + "/messages");
    if (!resp.ok()) {
        LOG_WARN("WhatsApp: poll failed - %s", resp.error.c_str());
        return;
    }
    
    Json result = resp.json();
    if (result.contains("messages") && result["messages"].is_array()) {
        for (const auto& msg : result["messages"]) {
            process_bridge_message(msg);
        }
    }
}

void WhatsAppChannel::process_bridge_message(const Json& msg) {
    Message m;
    m.channel = "whatsapp";
    m.id = msg.value("id", std::string(""));
    m.from = msg.value("from", std::string(""));
    m.from_name = msg.value("from_name", m.from);
    m.to = msg.value("to", std::string(""));
    m.text = msg.value("text", std::string(""));
    m.timestamp = msg.value("timestamp", int64_t(0));
    
    if (msg.value("is_group", false)) {
        m.chat_type = "group";
    } else {
        m.chat_type = "direct";
    }
    
    if (msg.contains("reply_to")) {
        m.reply_to_id = msg.value("reply_to", std::string(""));
    }
    
    LOG_DEBUG("WhatsApp: received message from %s: %s", 
              m.from_name.c_str(), m.text.c_str());
    
    emit_message(m);
}

} // namespace opencrank

// Export plugin for dynamic loading
OPENCRANK_DECLARE_PLUGIN(opencrank::WhatsAppChannel, "whatsapp", "1.0.0", 
                        "WhatsApp messaging channel", "channel")
