#include <opencrank/core/session.hpp>
#include <opencrank/core/utils.hpp>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <mutex>

namespace opencrank {

// ============ SessionKey static members ============

const char* SessionKey::DEFAULT_AGENT_ID = "default";
const char* SessionKey::DEFAULT_ACCOUNT_ID = "_default";
const char* SessionKey::DEFAULT_MAIN_KEY = "main";

std::string SessionKey::build(const std::string& agent_id,
                              const std::string& channel,
                              const std::string& account_id,
                              const RoutePeer* peer,
                              DMScope scope) {
    std::string safe_agent = sanitize_agent_id(agent_id.empty() ? DEFAULT_AGENT_ID : agent_id);
    std::string safe_channel = to_lower(trim(channel.empty() ? "unknown" : channel));
    std::string safe_account = trim(account_id.empty() ? DEFAULT_ACCOUNT_ID : account_id);
    
    std::ostringstream oss;
    oss << "agent:" << safe_agent << ":";
    
    if (!peer) {
        // No peer - use main session
        oss << DEFAULT_MAIN_KEY;
        return oss.str();
    }
    
    // Determine scope-based key format
    switch (scope) {
        case DMScope::MAIN:
            if (peer->kind == PeerKind::DM) {
                oss << DEFAULT_MAIN_KEY;
            } else {
                // Groups/channels get unique sessions
                oss << safe_channel << ":"
                    << (peer->kind == PeerKind::GROUP ? "group:" : "channel:")
                    << trim(peer->id);
            }
            break;
            
        case DMScope::PER_PEER:
            oss << (peer->kind == PeerKind::DM ? "dm:" : 
                   peer->kind == PeerKind::GROUP ? "group:" : "channel:")
                << trim(peer->id);
            break;
            
        case DMScope::PER_CHANNEL_PEER:
            oss << safe_channel << ":"
                << (peer->kind == PeerKind::DM ? "dm:" :
                   peer->kind == PeerKind::GROUP ? "group:" : "channel:")
                << trim(peer->id);
            break;
            
        case DMScope::PER_ACCOUNT_PEER:
            oss << safe_account << ":" << safe_channel << ":"
                << (peer->kind == PeerKind::DM ? "dm:" :
                   peer->kind == PeerKind::GROUP ? "group:" : "channel:")
                << trim(peer->id);
            break;
    }
    
    return to_lower(oss.str());
}

std::string SessionKey::build_main(const std::string& agent_id) {
    std::string safe_agent = sanitize_agent_id(agent_id.empty() ? DEFAULT_AGENT_ID : agent_id);
    return "agent:" + safe_agent + ":" + DEFAULT_MAIN_KEY;
}

SessionKey::Parsed SessionKey::parse(const std::string& session_key) {
    Parsed result;
    std::string raw = trim(session_key);
    
    if (raw.empty()) return result;
    
    std::vector<std::string> parts = split(raw, ':');
    if (parts.size() < 3) return result;
    
    if (parts[0] != "agent") return result;
    
    result.agent_id = trim(parts[1]);
    if (result.agent_id.empty()) return result;
    
    // Rest is everything after agent:id:
    std::vector<std::string> rest_parts(parts.begin() + 2, parts.end());
    result.rest = join(rest_parts, ":");
    
    if (result.rest.empty()) return result;
    
    result.valid = true;
    return result;
}

bool SessionKey::is_subagent_key(const std::string& session_key) {
    std::string raw = to_lower(trim(session_key));
    if (raw.empty()) return false;
    
    if (starts_with(raw, "subagent:")) return true;
    
    Parsed parsed = parse(raw);
    if (!parsed.valid) return false;
    
    return starts_with(to_lower(parsed.rest), "subagent:");
}

std::string SessionKey::normalize_agent_id(const std::string& agent_id) {
    return to_lower(trim(agent_id));
}

std::string SessionKey::sanitize_agent_id(const std::string& agent_id) {
    std::string normalized = normalize_agent_id(agent_id);
    
    // Remove invalid characters, keep only alphanumeric, dash, underscore
    std::string result;
    for (size_t i = 0; i < normalized.size(); ++i) {
        char c = normalized[i];
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
            result += c;
        }
    }
    
    return result.empty() ? DEFAULT_AGENT_ID : result;
}

// ============ Session ============

Session::Session() 
    : last_activity_(0) {
    touch();
}

Session::Session(const std::string& key)
    : key_(key)
    , last_activity_(0) {
    touch();
}

void Session::add_message(const ConversationMessage& msg) {
    history_.push_back(msg);
    touch();
}

void Session::clear_history() {
    history_.clear();
    touch();
}

void Session::limit_history(size_t max_messages) {
    if (history_.size() > max_messages) {
        history_.erase(history_.begin(), 
                       history_.begin() + static_cast<long>(history_.size() - max_messages));
    }
}

void Session::touch() {
    last_activity_ = current_timestamp();
}

void Session::set_data(const std::string& key, const std::string& value) {
    data_[key] = value;
}

std::string Session::get_data(const std::string& key, const std::string& default_value) const {
    std::map<std::string, std::string>::const_iterator it = data_.find(key);
    return it != data_.end() ? it->second : default_value;
}

bool Session::has_data(const std::string& key) const {
    return data_.find(key) != data_.end();
}

void Session::remove_data(const std::string& key) {
    data_.erase(key);
}

// ============ SessionManager ============

SessionManager& SessionManager::instance() {
    static SessionManager manager;
    return manager;
}

SessionManager::SessionManager() 
    : dm_scope_(DMScope::MAIN)
    , max_history_(20) {}

Session& SessionManager::get_session(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<std::string, Session>::iterator it = sessions_.find(key);
    if (it == sessions_.end()) {
        Session new_session(key);
        sessions_[key] = new_session;
        return sessions_[key];
    }
    it->second.touch();
    return it->second;
}

bool SessionManager::has_session(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.find(key) != sessions_.end();
}

void SessionManager::remove_session(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(key);
}

Session& SessionManager::get_session_for_message(const Message& msg, const std::string& agent_id) {
    // Determine peer kind from message
    PeerKind kind = PeerKind::DM;
    if (msg.chat_type == "group") {
        kind = PeerKind::GROUP;
    } else if (msg.chat_type == "channel") {
        kind = PeerKind::CHANNEL;
    }
    
    RoutePeer peer(kind, msg.to);
    
    std::string session_key = SessionKey::build(
        agent_id.empty() ? SessionKey::DEFAULT_AGENT_ID : agent_id,
        msg.channel,
        SessionKey::DEFAULT_ACCOUNT_ID,
        &peer,
        dm_scope_
    );
    
    Session& session = get_session(session_key);
    session.set_channel(msg.channel);
    session.set_peer_id(msg.from);
    session.set_agent_id(agent_id.empty() ? SessionKey::DEFAULT_AGENT_ID : agent_id);
    
    // Apply history limit
    session.limit_history(max_history_);
    
    return session;
}

void SessionManager::clear_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.clear();
}

size_t SessionManager::cleanup_inactive(int64_t max_age_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t now = current_timestamp();
    size_t removed = 0;
    
    std::map<std::string, Session>::iterator it = sessions_.begin();
    while (it != sessions_.end()) {
        if (now - it->second.last_activity() > max_age_seconds) {
            it = sessions_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    
    return removed;
}

std::vector<std::string> SessionManager::session_keys() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> keys;
    for (std::map<std::string, Session>::const_iterator it = sessions_.begin();
         it != sessions_.end(); ++it) {
        keys.push_back(it->first);
    }
    return keys;
}

// ============ Route resolution ============

ResolvedRoute resolve_route(const std::string& channel,
                           const std::string& account_id,
                           const RoutePeer* peer,
                           const std::string& default_agent_id,
                           DMScope scope) {
    ResolvedRoute result;
    
    result.channel = to_lower(trim(channel));
    result.account_id = trim(account_id.empty() ? SessionKey::DEFAULT_ACCOUNT_ID : account_id);
    result.agent_id = SessionKey::sanitize_agent_id(
        default_agent_id.empty() ? SessionKey::DEFAULT_AGENT_ID : default_agent_id
    );
    
    result.session_key = SessionKey::build(
        result.agent_id,
        result.channel,
        result.account_id,
        peer,
        scope
    );
    
    result.main_session_key = SessionKey::build_main(result.agent_id);
    result.matched_by = "default";
    
    return result;
}

} // namespace opencrank
