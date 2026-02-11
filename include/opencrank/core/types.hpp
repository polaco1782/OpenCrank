#ifndef opencrank_CORE_TYPES_HPP
#define opencrank_CORE_TYPES_HPP

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <cstdint>
#include <ctime>

namespace opencrank {

// Message structure for channel communication
struct Message {
    std::string id;              // Unique message ID
    std::string channel;         // Channel name (telegram, discord, etc.)
    std::string from;            // Sender identifier
    std::string from_name;       // Human-readable sender name
    std::string to;              // Recipient/chat identifier
    std::string text;            // Message text content
    std::string chat_type;       // "direct", "group", "channel"
    int64_t timestamp;           // Unix timestamp
    std::string reply_to_id;     // ID of message being replied to (optional)
    std::string media_url;       // Attached media URL (optional)
    
    Message() : timestamp(0) {}
};

// Result of sending a message
struct SendResult {
    bool success;
    std::string message_id;
    std::string error;
    
    SendResult() : success(false) {}
    
    static SendResult ok(const std::string& msg_id) {
        SendResult r;
        r.success = true;
        r.message_id = msg_id;
        return r;
    }
    
    static SendResult fail(const std::string& err) {
        SendResult r;
        r.success = false;
        r.error = err;
        return r;
    }
};

// Channel capabilities
struct ChannelCapabilities {
    bool supports_groups;
    bool supports_reactions;
    bool supports_media;
    bool supports_edit;
    bool supports_delete;
    bool supports_threads;
    bool supports_typing;
    
    ChannelCapabilities() 
        : supports_groups(false)
        , supports_reactions(false)
        , supports_media(false)
        , supports_edit(false)
        , supports_delete(false)
        , supports_threads(false)
        , supports_typing(false) {}
};

// Channel status
enum class ChannelStatus {
    STOPPED,
    STARTING,
    RUNNING,
    STOPPING,
    ERROR
};

// Callback types
using MessageCallback = std::function<void(const Message&)>;
using ErrorCallback = std::function<void(const std::string& channel, const std::string& error)>;

// Forward declarations
class Session;

// Command handler function type
typedef std::string (*CommandHandlerFunc)(const Message& msg, Session& session, const std::string& args);

// Command definition for plugin registration
struct CommandDef {
    std::string command;      // Command name (e.g., "/ping")
    std::string description;  // Help text
    CommandHandlerFunc handler;
    
    CommandDef() : handler(NULL) {}
    CommandDef(const std::string& cmd, const std::string& desc, CommandHandlerFunc h)
        : command(cmd), description(desc), handler(h) {}
};

} // namespace opencrank

#endif // opencrank_CORE_TYPES_HPP
