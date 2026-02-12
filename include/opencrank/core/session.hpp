#ifndef opencrank_CORE_SESSION_HPP
#define opencrank_CORE_SESSION_HPP

#include <opencrank/core/types.hpp>
#include <opencrank/ai/ai.hpp>
#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include <mutex>

namespace opencrank {

// Session scope for DM handling
enum class DMScope {
    MAIN,              // All DMs share one session (default)
    PER_PEER,          // Separate session per peer across all channels
    PER_CHANNEL_PEER,  // Separate session per channel+peer
    PER_ACCOUNT_PEER   // Separate session per account+channel+peer
};

// Peer kind for routing
enum class PeerKind {
    DM,      // Direct message
    GROUP,   // Group chat
    CHANNEL  // Broadcast channel
};

// Represents a peer (contact/chat)
struct RoutePeer {
    PeerKind kind;
    std::string id;
    
    RoutePeer() : kind(PeerKind::DM) {}
    RoutePeer(PeerKind k, const std::string& i) : kind(k), id(i) {}
    
    static RoutePeer dm(const std::string& id) { return RoutePeer(PeerKind::DM, id); }
    static RoutePeer group(const std::string& id) { return RoutePeer(PeerKind::GROUP, id); }
    static RoutePeer channel(const std::string& id) { return RoutePeer(PeerKind::CHANNEL, id); }
};

// Session key utilities
class SessionKey {
public:
    static inline constexpr const char* DEFAULT_AGENT_ID = "default";
    static inline constexpr const char* DEFAULT_ACCOUNT_ID = "_default";
    static inline constexpr const char* DEFAULT_MAIN_KEY = "main";
    
    // Build a session key from components
    static std::string build(const std::string& agent_id,
                            const std::string& channel,
                            const std::string& account_id,
                            const RoutePeer* peer,
                            DMScope scope);
    
    // Build a main session key (for agent's default session)
    static std::string build_main(const std::string& agent_id);
    
    // Parse a session key into components
    struct Parsed {
        std::string agent_id;
        std::string rest;
        bool valid;
        
        Parsed() : valid(false) {}
    };
    
    static Parsed parse(const std::string& session_key);
    
    // Check if session key is for a subagent
    static bool is_subagent_key(const std::string& session_key);
    
    // Normalize agent ID (lowercase, trim)
    static std::string normalize_agent_id(const std::string& agent_id);
    
    // Sanitize agent ID for use in session key
    static std::string sanitize_agent_id(const std::string& agent_id);
};

// A user session with conversation history and state
class Session {
public:
    Session();
    explicit Session(const std::string& key);
    
    // Session key
    const std::string& key() const { return key_; }
    void set_key(const std::string& key) { key_ = key; }
    
    // Agent ID
    const std::string& agent_id() const { return agent_id_; }
    void set_agent_id(const std::string& id) { agent_id_ = id; }
    
    // Channel
    const std::string& channel() const { return channel_; }
    void set_channel(const std::string& ch) { channel_ = ch; }
    
    // Peer info
    const std::string& peer_id() const { return peer_id_; }
    void set_peer_id(const std::string& id) { peer_id_ = id; }
    
    // Conversation history
    const std::vector<ConversationMessage>& history() const { return history_; }
    std::vector<ConversationMessage>& history() { return history_; }
    
    // Add a message to history
    void add_message(const ConversationMessage& msg);
    
    // Clear conversation history
    void clear_history();
    
    // Limit history to max messages (keeps most recent)
    void limit_history(size_t max_messages);
    
    // Last activity timestamp
    int64_t last_activity() const { return last_activity_; }
    void touch();
    
    // Custom data storage
    void set_data(const std::string& key, const std::string& value);
    std::string get_data(const std::string& key, const std::string& default_value = "") const;
    bool has_data(const std::string& key) const;
    void remove_data(const std::string& key);
    
private:
    std::string key_;
    std::string agent_id_;
    std::string channel_;
    std::string peer_id_;
    std::vector<ConversationMessage> history_;
    int64_t last_activity_;
    std::map<std::string, std::string> data_;
};

// Session manager - manages all active sessions
class SessionManager {
public:
    static SessionManager& instance();
    
    // Get or create a session by key
    Session& get_session(const std::string& key);
    
    // Check if session exists
    bool has_session(const std::string& key) const;
    
    // Remove a session
    void remove_session(const std::string& key);
    
    // Get session for a message
    Session& get_session_for_message(const Message& msg, const std::string& agent_id = "");
    
    // Clear all sessions
    void clear_all();
    
    // Clean up inactive sessions (older than max_age_seconds)
    size_t cleanup_inactive(int64_t max_age_seconds);
    
    // Get all session keys
    std::vector<std::string> session_keys() const;
    
    // Session count
    size_t session_count() const { return sessions_.size(); }
    
    // DM scope setting
    DMScope dm_scope() const { return dm_scope_; }
    void set_dm_scope(DMScope scope) { dm_scope_ = scope; }
    
    // Max history per session
    size_t max_history() const { return max_history_; }
    void set_max_history(size_t max) { max_history_ = max; }

private:
    SessionManager();
    SessionManager(const SessionManager&);
    SessionManager& operator=(const SessionManager&);
    
    std::map<std::string, Session> sessions_;
    mutable std::mutex mutex_;
    DMScope dm_scope_;
    size_t max_history_;
};

// Route resolution result
struct ResolvedRoute {
    std::string agent_id;
    std::string channel;
    std::string account_id;
    std::string session_key;
    std::string main_session_key;
    std::string matched_by;  // "binding.peer", "binding.channel", "default"
};

} // namespace opencrank

#endif // opencrank_CORE_SESSION_HPP
