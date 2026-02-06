#include <opencrank/plugins/telegram/telegram.hpp>
#include <opencrank/core/loader.hpp>
#include <sstream>

namespace opencrank {

namespace {
std::string escape_html(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        switch (text[i]) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            default: escaped += text[i]; break;
        }
    }
    return escaped;
}
} // namespace

TelegramChannel::TelegramChannel() 
    : status_(ChannelStatus::STOPPED)
    , last_update_id_(0)
    , poll_timeout_(30)
    , should_stop_polling_(false) {}

const char* TelegramChannel::name() const { return "telegram"; }
const char* TelegramChannel::version() const { return "1.0.0"; }
const char* TelegramChannel::description() const { return "Telegram Bot API channel"; }
const char* TelegramChannel::channel_id() const { return "telegram"; }

ChannelCapabilities TelegramChannel::capabilities() const {
    ChannelCapabilities caps;
    caps.supports_groups = true;
    caps.supports_reactions = true;
    caps.supports_media = true;
    caps.supports_edit = true;
    caps.supports_delete = true;
    caps.supports_typing = true;
    return caps;
}

bool TelegramChannel::init(const Config& cfg) {
    bot_token_ = cfg.get_channel_string("telegram", "bot_token");
    
    if (bot_token_.empty()) {
        LOG_WARN("Telegram: bot_token not configured in config.json");
        return false;
    }
    
    api_base_ = "https://api.telegram.org/bot" + bot_token_;
    
    poll_timeout_ = static_cast<int>(cfg.get_channel_string("telegram", "poll_timeout", "30")[0] - '0') * 10;
    if (poll_timeout_ <= 0 || poll_timeout_ > 60) poll_timeout_ = 30;
    
    LOG_INFO("Telegram: initialized with poll_timeout=%d", poll_timeout_);
    initialized_ = true;
    return true;
}

void TelegramChannel::shutdown() {
    stop();
    initialized_ = false;
    LOG_INFO("Telegram: shutdown");
}

bool TelegramChannel::start() {
    if (!initialized_) {
        LOG_ERROR("Telegram: cannot start - not initialized");
        return false;
    }
    
    status_ = ChannelStatus::STARTING;
    
    HttpResponse resp = http_.get(api_base_ + "/getMe");
    if (!resp.ok()) {
        LOG_ERROR("Telegram: failed to connect - %s", resp.error.c_str());
        status_ = ChannelStatus::ERROR;
        return false;
    }
    
    Json result = resp.json();
    if (!result.value("ok", false)) {
        LOG_ERROR("Telegram: API error - %s", result.value("description", std::string("unknown")).c_str());
        status_ = ChannelStatus::ERROR;
        return false;
    }
    
    const Json& bot_info = result["result"];
    bot_id_ = bot_info.value("id", int64_t(0));
    bot_username_ = bot_info.value("username", std::string(""));
    
    LOG_INFO("Telegram: connected as @%s (id=%lld)", 
             bot_username_.c_str(), (long long)bot_id_);
    
    // Start polling thread
    should_stop_polling_ = false;
    poll_thread_ = std::thread(&TelegramChannel::polling_loop, this);
    
    status_ = ChannelStatus::RUNNING;
    LOG_INFO("Telegram: polling thread started");
    return true;
}

bool TelegramChannel::stop() {
    if (status_ == ChannelStatus::STOPPED) return true;
    
    status_ = ChannelStatus::STOPPING;
    
    // Stop polling thread
    should_stop_polling_ = true;
    if (poll_thread_.joinable()) {
        poll_thread_.join();
        LOG_INFO("Telegram: polling thread stopped");
    }
    
    status_ = ChannelStatus::STOPPED;
    LOG_INFO("Telegram: stopped");
    return true;
}

ChannelStatus TelegramChannel::status() const { return status_; }

SendResult TelegramChannel::send_message(const std::string& to, const std::string& text) {
    return send_message_impl(to, text, 0);
}

SendResult TelegramChannel::send_message(const std::string& to, const std::string& text,
                                         const std::string& reply_to) {
    int64_t reply_id = 0;
    if (!reply_to.empty()) {
        reply_id = std::strtoll(reply_to.c_str(), NULL, 10);
    }
    return send_message_impl(to, text, reply_id);
}

SendResult TelegramChannel::send_typing_action(const std::string& to) {
    Json params = Json::object();
    params["chat_id"] = to;
    params["action"] = "typing";
    
    LOG_DEBUG("Telegram: sending typing action to chat_id=%s", to.c_str());
    
    // Use a quick timeout for typing actions (they're fire-and-forget)
    http_send_.set_timeout(3000);
    HttpResponse resp = http_send_.post_json(api_base_ + "/sendChatAction", params);
    
    if (!resp.ok()) {
        LOG_WARN("Telegram: typing action HTTP failed - %s", resp.error.c_str());
        return SendResult::fail("HTTP error: " + resp.error);
    }
    
    Json result = resp.json();
    bool api_ok = result.value("ok", false);
    
    if (!api_ok) {
        std::string desc = result.value("description", std::string("unknown"));
        LOG_WARN("Telegram: typing action API error - %s (chat_id=%s)", desc.c_str(), to.c_str());
        return SendResult::fail("API error: " + desc);
    }
    
    LOG_INFO("Telegram: typing action sent successfully to %s", to.c_str());
    return SendResult::ok("");
}

void TelegramChannel::poll() {
    // No-op: polling now happens in dedicated thread
    // This method kept for API compatibility
}

void TelegramChannel::polling_loop() {
    LOG_INFO("Telegram: polling loop started");
    
    while (!should_stop_polling_ && status_ == ChannelStatus::RUNNING) {
        std::ostringstream url;
        url << api_base_ << "/getUpdates?timeout=" << poll_timeout_;
        if (last_update_id_ > 0) {
            url << "&offset=" << (last_update_id_ + 1);
        }
        url << "&allowed_updates=" << "[\"message\",\"edited_message\"]";
        
        http_.set_timeout((poll_timeout_ + 5) * 1000);
        
        HttpResponse resp = http_.get(url.str());
        
        if (!resp.ok()) {
            if (!should_stop_polling_) {
                LOG_WARN("Telegram: poll failed - %s", resp.error.c_str());
            }
            continue;
        }
        
        Json result = resp.json();
        if (!result.value("ok", false)) {
            LOG_WARN("Telegram: poll API error - %s", result.value("description", std::string("unknown")).c_str());
            continue;
        }
        
        if (result.contains("result") && result["result"].is_array()) {
            for (const auto& update : result["result"]) {
                process_update(update);
            }
        }
    }
    
    LOG_INFO("Telegram: polling loop exited");
}

SendResult TelegramChannel::send_message_impl(const std::string& to, const std::string& text, int64_t reply_to) {
    Json params = Json::object();
    params["chat_id"] = to;
    params["text"] = escape_html(text);
    params["parse_mode"] = "HTML";
    
    if (reply_to > 0) {
        params["reply_to_message_id"] = reply_to;
    }
    
    // Use separate http_send_ instance with 10s timeout
    http_send_.set_timeout(10000);
    HttpResponse resp = http_send_.post_json(api_base_ + "/sendMessage", params);
    
    if (!resp.ok()) {
        return SendResult::fail("HTTP error: " + resp.error);
    }
    
    Json result = resp.json();
    if (!result.value("ok", false)) {
        return SendResult::fail("API error: " + result.value("description", std::string("unknown")));
    }
    
    std::ostringstream msg_id;
    msg_id << result["result"].value("message_id", int64_t(0));
    
    LOG_DEBUG("Telegram: sent message to %s (id=%s)", to.c_str(), msg_id.str().c_str());
    return SendResult::ok(msg_id.str());
}

void TelegramChannel::process_update(const Json& update) {
    int64_t update_id = update.value("update_id", int64_t(0));
    if (update_id > last_update_id_) {
        last_update_id_ = update_id;
    }
    
    const Json* msg_ptr = nullptr;
    if (update.contains("message")) {
        msg_ptr = &update["message"];
    } else if (update.contains("edited_message")) {
        msg_ptr = &update["edited_message"];
    }
    
    if (!msg_ptr || msg_ptr->is_null()) return;
    const Json& msg = *msg_ptr;
    
    Message m;
    m.channel = "telegram";
    
    std::ostringstream id_ss;
    id_ss << msg.value("message_id", int64_t(0));
    m.id = id_ss.str();
    
    const Json& chat = msg["chat"];
    std::ostringstream chat_id;
    chat_id << chat.value("id", int64_t(0));
    m.to = chat_id.str();
    
    std::string chat_type = chat.value("type", std::string(""));
    if (chat_type == "private") {
        m.chat_type = "direct";
    } else if (chat_type == "group" || chat_type == "supergroup") {
        m.chat_type = "group";
    } else if (chat_type == "channel") {
        m.chat_type = "channel";
    }
    
    if (msg.contains("from") && !msg["from"].is_null()) {
        const Json& from = msg["from"];
        std::ostringstream from_id;
        from_id << from.value("id", int64_t(0));
        m.from = from_id.str();
        
        std::string first = from.value("first_name", std::string(""));
        std::string last = from.value("last_name", std::string(""));
        m.from_name = first;
        if (!last.empty()) {
            m.from_name += " " + last;
        }
        std::string username = from.value("username", std::string(""));
        if (!username.empty()) {
            m.from_name += " (@" + username + ")";
        }
    }
    
    m.text = msg.value("text", std::string(""));
    if (m.text.empty()) {
        m.text = msg.value("caption", std::string(""));
    }
    
    if (msg.contains("reply_to_message") && !msg["reply_to_message"].is_null()) {
        const Json& reply = msg["reply_to_message"];
        std::ostringstream reply_id;
        reply_id << reply.value("message_id", int64_t(0));
        m.reply_to_id = reply_id.str();
    }
    
    m.timestamp = msg.value("date", int64_t(0));
    
    if (!m.text.empty()) {
        emit_message(m);
    }
}

} // namespace opencrank

// Export plugin for dynamic loading
OPENCRANK_DECLARE_PLUGIN(opencrank::TelegramChannel, "telegram", "1.0.0", 
                        "Telegram Bot API channel", "channel")
