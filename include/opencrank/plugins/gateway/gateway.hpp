#ifndef opencrank_PLUGINS_GATEWAY_GATEWAY_HPP
#define opencrank_PLUGINS_GATEWAY_GATEWAY_HPP

#include "../../core/plugin.hpp"
#include "../../core/channel.hpp"
#include "../../core/types.hpp"
#include "../../core/json.hpp"
#include <string>
#include <map>
#include <vector>
#include <set>

namespace opencrank {

// Forward declarations
class WebSocketServer;
class GatewayClient;

// Gateway Server Plugin
// Implements a WebSocket-based gateway for remote agent control
// Compatible with opencrank gateway protocol
class GatewayPlugin : public ChannelPlugin {
public:
    GatewayPlugin();
    virtual ~GatewayPlugin();
    
    // Plugin interface
    virtual const char* name() const { return "gateway"; }
    virtual const char* version() const { return "1.0.0"; }
    virtual const char* description() const { 
        return "WebSocket gateway server for remote agent control"; 
    }
    
    virtual bool init(const Config& cfg);
    virtual void shutdown();
    virtual void poll();  // Poll WebSocket server
    
    // ChannelPlugin interface
    virtual const char* channel_id() const { return "gateway"; }
    virtual ChannelCapabilities capabilities() const;
    virtual ChannelStatus status() const { return running_ ? ChannelStatus::RUNNING : ChannelStatus::STOPPED; }
    virtual bool start();  // Start the gateway server
    virtual bool stop();   // Stop the gateway server
    virtual SendResult send_message(const std::string& to, const std::string& text);
    virtual SendResult send_message(const std::string& to, const std::string& text, const std::string& reply_to);
    
    // Receive all incoming messages for routing to gateway clients
    virtual void on_incoming_message(const Message& msg) override;
    
    // Receive typing indicator events
    virtual void on_typing_indicator(const std::string& channel_id,
                                     const std::string& chat_id,
                                     bool typing) override;
    
    // Get server status
    bool is_running() const { return running_; }
    int port() const { return port_; }
    size_t client_count() const;
    const std::string& index_filename() const { return index_filename_; }
    
    // Broadcast events to all connected clients
    void broadcast(const std::string& event, const Json& payload);
    
    // Send typing indicator event
    void send_typing_event(const std::string& channel_id, 
                          const std::string& chat_id, 
                          bool typing);
    
    // Message routing - called by channel plugins to broadcast messages to gateway
    void route_incoming_message(const Message& msg);
    
    // Protocol handlers (public for WebSocketServer callback)
    void handle_client_connect(GatewayClient* client);
    void handle_client_disconnect(GatewayClient* client);
    void handle_client_message(GatewayClient* client, const std::string& msg);
    
private:
    bool running_;
    int port_;
    std::string bind_host_;
    std::string auth_token_;
    std::string index_filename_;
    
    // WebSocket server (implementation in .cpp)
    WebSocketServer* ws_server_;
    
    // Connected clients
    std::set<GatewayClient*> clients_;
    
    // Track most recent active chat for routing outgoing messages
    std::string recent_chat_id_;
    
    // Gateway protocol methods
    Json handle_hello(GatewayClient* client, const Json& params);
    Json handle_chat_send(GatewayClient* client, const Json& params);
    Json handle_config_get(GatewayClient* client, const Json& params);
    Json handle_health_get(GatewayClient* client, const Json& params);
    Json handle_models_list(GatewayClient* client, const Json& params);
};

// Client connection state
class GatewayClient {
public:
    GatewayClient(void* ws_connection, const std::string& conn_id);
    ~GatewayClient();
    
    const std::string& conn_id() const { return conn_id_; }
    bool is_authenticated() const { return authenticated_; }
    void set_authenticated(bool auth) { authenticated_ = auth; }
    
    // Send message to client
    bool send(const std::string& message);
    bool send_json(const Json& data);
    
    // Protocol info
    int protocol_version() const { return protocol_version_; }
    void set_protocol_version(int ver) { protocol_version_ = ver; }
    
    const std::string& client_id() const { return client_id_; }
    void set_client_id(const std::string& id) { client_id_ = id; }
    
private:
    void* ws_connection_;  // Opaque WebSocket connection handle
    std::string conn_id_;
    std::string client_id_;
    bool authenticated_;
    int protocol_version_;
};

} // namespace opencrank

// Plugin factory function
extern "C" {
    opencrank::Plugin* create_plugin();
}

#endif // opencrank_PLUGINS_GATEWAY_GATEWAY_HPP
