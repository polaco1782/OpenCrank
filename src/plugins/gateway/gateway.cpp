/*
 * Gateway Plugin Implementation
 * 
 * WebSocket-based gateway server for remote agent control.
 * Compatible with OpenCrank gateway protocol.
 * 
 * Uses Crow (https://crowcpp.org) for HTTP and WebSocket functionality.
 */

#include <opencrank/plugins/gateway/gateway.hpp>
#include <opencrank/core/loader.hpp>
#include <opencrank/core/logger.hpp>
#include <opencrank/core/utils.hpp>
#include <opencrank/core/registry.hpp>
#include <opencrank/core/channel.hpp>

// Crow header-only library (C++17 required)
#include "deps/crow_all.h"

#include <cstring>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace opencrank {

// ============================================================================
// WebSocket Server Implementation (using Crow)
// ============================================================================

class WebSocketServer {
public:
    WebSocketServer(GatewayPlugin* plugin) 
        : plugin_(plugin)
        , port_(0)
        , running_(false)
        , app_() {}
    
    ~WebSocketServer() {
        stop();
    }
    
    bool start(int port, const std::string& bind_host) {
        port_ = port;
        bind_host_ = bind_host;
        
        // Configure routes
        setup_routes();
        
        // Start server in background thread
        running_ = true;
        server_thread_ = std::thread([this, port, bind_host]() {
            try {
                if (bind_host.empty() || bind_host == "0.0.0.0") {
                    app_.bindaddr("0.0.0.0").port(port).multithreaded().run();
                } else {
                    app_.bindaddr(bind_host).port(port).multithreaded().run();
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Gateway server error: %s", e.what());
            }
            running_ = false;
        });
        
        // Wait a bit for server to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        LOG_INFO("Gateway server started on %s:%d", 
                 bind_host.empty() ? "0.0.0.0" : bind_host.c_str(), port);
        return true;
    }
    
    void stop() {
        if (running_) {
            running_ = false;
            app_.stop();
            
            if (server_thread_.joinable()) {
                server_thread_.join();
            }
            
            // Clear all connections
            std::lock_guard<std::mutex> lock(connections_mutex_);
            connections_.clear();
        }
    }
    
    void poll(int /* timeout_ms */ = 10) {
        // Crow handles its own event loop in the background thread
        // Nothing to do here - kept for API compatibility
    }
    
    bool send_to_client(crow::websocket::connection* conn, const std::string& data) {
        if (!conn) return false;
        
        try {
            conn->send_text(data);
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to send to WebSocket client: %s", e.what());
            return false;
        }
    }
    
    // Get connection by client pointer (for sending from GatewayClient)
    crow::websocket::connection* get_connection(void* ws_conn) {
        return static_cast<crow::websocket::connection*>(ws_conn);
    }
    
private:
    void setup_routes() {
        // Serve static HTML control UI
        CROW_ROUTE(app_, "/")
        ([this]() {
            crow::response res;
            res.redirect("/index.html");
            return res;
        });
        
        CROW_ROUTE(app_, "/index.html")
        ([this]() {
            crow::response res;
            std::ifstream html_file(plugin_->index_filename());
            if (html_file.is_open()) {
                std::stringstream buffer;
                buffer << html_file.rdbuf();
                res.set_header("Content-Type", "text/html; charset=utf-8");
                res.body = buffer.str();
                html_file.close();

                LOG_DEBUG("[Gateway] Served control UI to HTTP client");
            }
            else {
                res.code = 404;
                res.body = "404 Not Found";
                LOG_WARN("[Gateway] Control UI HTML file not found");
            }

            return res;
        });
        
        // WebSocket endpoint
        CROW_WEBSOCKET_ROUTE(app_, "/ws")
            .onopen([this](crow::websocket::connection& conn) {
                on_ws_open(conn);
            })
            .onclose([this](crow::websocket::connection& conn, const std::string& reason) {
                on_ws_close(conn, reason);
            })
            .onmessage([this](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
                on_ws_message(conn, data, is_binary);
            });
    }
    
    void on_ws_open(crow::websocket::connection& conn) {
        // Generate connection ID
        std::ostringstream oss;
        oss << "conn_" << &conn << "_" << std::time(nullptr);
        std::string conn_id = oss.str();
        
        // Create client and track connection
        GatewayClient* client = new GatewayClient(&conn, conn_id);
        
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            connections_[&conn] = client;
        }
        
        plugin_->handle_client_connect(client);
        LOG_DEBUG("[Gateway] WebSocket client connected: %s", conn_id.c_str());
    }
    
    void on_ws_close(crow::websocket::connection& conn, const std::string& reason) {
        GatewayClient* client = nullptr;
        
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            auto it = connections_.find(&conn);
            if (it != connections_.end()) {
                client = it->second;
                connections_.erase(it);
            }
        }
        
        if (client) {
            LOG_DEBUG("[Gateway] WebSocket client disconnected: %s (reason: %s)", 
                     client->conn_id().c_str(), reason.c_str());
            plugin_->handle_client_disconnect(client);
            delete client;
        }
    }
    
    void on_ws_message(crow::websocket::connection& conn, const std::string& data, bool /* is_binary */) {
        GatewayClient* client = nullptr;
        
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            auto it = connections_.find(&conn);
            if (it != connections_.end()) {
                client = it->second;
            }
        }
        
        if (client) {
            plugin_->handle_client_message(client, data);
        }
    }
    
    GatewayPlugin* plugin_;
    int port_;
    std::string bind_host_;
    std::atomic<bool> running_;
    crow::SimpleApp app_;
    std::thread server_thread_;
    
    // Track WebSocket connections
    std::mutex connections_mutex_;
    std::unordered_map<crow::websocket::connection*, GatewayClient*> connections_;
};

// ============================================================================
// GatewayClient Implementation
// ============================================================================

GatewayClient::GatewayClient(void* ws_connection, const std::string& conn_id)
    : ws_connection_(ws_connection)
    , conn_id_(conn_id)
    , authenticated_(false)
    , protocol_version_(1) {
}

GatewayClient::~GatewayClient() {
}

bool GatewayClient::send(const std::string& message) {
    if (!ws_connection_) return false;
    
    try {
        auto* conn = static_cast<crow::websocket::connection*>(ws_connection_);
        conn->send_text(message);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to send WebSocket message: %s", e.what());
        return false;
    }
}

bool GatewayClient::send_json(const Json& data) {
    return send(data.dump());
}

// ============================================================================
// GatewayPlugin Implementation
// ============================================================================

GatewayPlugin::GatewayPlugin()
    : running_(false)
    , port_(18789)
    , bind_host_("127.0.0.1")
    , index_filename_("ui/control_ui.html")
    , ws_server_(nullptr) {
    initialized_ = false;
}

GatewayPlugin::~GatewayPlugin() {
    shutdown();
}

bool GatewayPlugin::init(const Config& cfg) {
    if (initialized_) return true;
    
    LOG_INFO("Initializing gateway plugin (Crow backend)...");
    
    // Load configuration
    port_ = cfg.get_int("gateway.port", 18789);
    bind_host_ = cfg.get_string("gateway.bind", "127.0.0.1");
    auth_token_ = cfg.get_string("gateway.auth.token", "");
    index_filename_ = cfg.get_string("gateway.index_file", "ui/control_ui.html");
    
    
    LOG_INFO("Gateway config: port=%d, bind=%s, auth=%s", 
             port_, bind_host_.c_str(), auth_token_.empty() ? "disabled" : "enabled");
    
    // Create WebSocket server (but don't start yet)
    ws_server_ = new WebSocketServer(this);
    
    initialized_ = true;
    LOG_INFO("Gateway plugin initialized (will start on first poll)");
    return true;
}

void GatewayPlugin::shutdown() {
    if (!initialized_) return;
    
    LOG_INFO("Shutting down gateway plugin...");
    
    stop();
    
    if (ws_server_) {
        delete ws_server_;
        ws_server_ = nullptr;
    }
    
    // Clean up clients
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        delete *it;
    }
    clients_.clear();
    
    initialized_ = false;
}

bool GatewayPlugin::start(int port) {
    if (running_) {
        LOG_WARN("Gateway already running");
        return true;
    }
    
    if (port > 0) {
        port_ = port;
    }
    
    if (!ws_server_) {
        LOG_ERROR("Gateway not initialized");
        return false;
    }
    
    if (!ws_server_->start(port_, bind_host_)) {
        LOG_ERROR("Failed to start gateway server");
        return false;
    }
    
    running_ = true;
    LOG_INFO("Gateway started on http://%s:%d (WebSocket on /ws)", 
             bind_host_.c_str(), port_);
    return true;
}

void GatewayPlugin::stop() {
    if (!running_) return;
    
    LOG_INFO("Stopping gateway...");
    
    if (ws_server_) {
        ws_server_->stop();
    }
    
    running_ = false;
}

void GatewayPlugin::poll() {
    // Auto-start on first poll if not running
    if (!running_ && initialized_ && ws_server_) {
        start(port_);
    }
    
    // Crow runs in its own thread, no polling needed
    if (running_ && ws_server_) {
        ws_server_->poll(10);  // No-op but kept for API compatibility
    }
}

size_t GatewayPlugin::client_count() const {
    return clients_.size();
}

void GatewayPlugin::broadcast(const std::string& event, const Json& payload) {
    Json message = Json::object();
    message["type"] = "event";
    message["event"] = event;
    message["payload"] = payload;
    
    std::string data = message.dump();
    
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        (*it)->send(data);
    }
}

void GatewayPlugin::send_typing_event(const std::string& channel_id, 
                                      const std::string& chat_id, 
                                      bool typing) {
    Json payload = Json::object();
    payload["channel"] = channel_id;
    payload["chat_id"] = chat_id;
    payload["typing"] = typing;
    
    broadcast("chat.typing", payload);
    
    LOG_DEBUG("[Gateway] Sent typing=%s event for %s:%s to %zu clients", 
              typing ? "true" : "false", channel_id.c_str(), chat_id.c_str(), clients_.size());
}

// ============================================================================
// Protocol Handlers
// ============================================================================

void GatewayPlugin::handle_client_connect(GatewayClient* client) {
    clients_.insert(client);
    
    LOG_INFO("Gateway client connected (total: %zu)", clients_.size());
}

void GatewayPlugin::handle_client_disconnect(GatewayClient* client) {
    clients_.erase(client);
    
    LOG_INFO("Gateway client disconnected (remaining: %zu)", clients_.size());
}

void GatewayPlugin::handle_client_message(GatewayClient* client, 
                                          const std::string& msg) {
    // Parse JSON message
    Json request;
    try {
        request = Json::parse(msg);
    } catch (const std::exception& e) {
        LOG_ERROR("Invalid JSON from client %s: %s", 
                 client->conn_id().c_str(), e.what());
        return;
    }
    
    // Check message format
    if (!request.is_object() || !request.contains("type")) {
        LOG_ERROR("Invalid message format from client %s", 
                 client->conn_id().c_str());
        return;
    }
    
    std::string type = request.value("type", std::string(""));
    
    // Handle different message types
    Json response;
    
    if (type == "hello") {
        // Initial handshake
        Json params = request.contains("params") ? request["params"] : Json::object();
        response = handle_hello(client, params);
    }
    else if (type == "call") {
        // RPC method call
        std::string method = request.value("method", std::string(""));
        Json params = request.contains("params") ? request["params"] : Json::object();
        std::string id = request.value("id", std::string(""));
        
        // Check authentication for protected methods
        if (!auth_token_.empty() && !client->is_authenticated() && 
            method != "auth.login") {
            response = Json::object();
            response["type"] = "error";
            response["id"] = id;
            Json error_obj = Json::object();
            error_obj["code"] = "AUTH_REQUIRED";
            error_obj["message"] = "Authentication required";
            response["error"] = error_obj;
        } else {
            // Route to method handler
            Json result;
            
            if (method == "chat.send") {
                result = handle_chat_send(client, params);
            } else if (method == "config.get") {
                result = handle_config_get(client, params);
            } else if (method == "health.get") {
                result = handle_health_get(client, params);
            } else if (method == "models.list") {
                result = handle_models_list(client, params);
            } else {
                result = Json::object();
                result["error"] = "Method not found: " + method;
            }
            
            response = Json::object();
            response["type"] = "result";
            response["id"] = id;
            response["result"] = result;
        }
    }
    else {
        LOG_WARN("Unknown message type '%s' from client %s", 
                type.c_str(), client->conn_id().c_str());
        return;
    }
    
    // Send response
    if (response.is_object()) {
        client->send_json(response);
    }
}

Json GatewayPlugin::handle_hello(GatewayClient* client, const Json& params) {
    // Extract protocol version
    int min_protocol = params.value("minProtocol", 1);
    int max_protocol = params.value("maxProtocol", 1);
    
    // We support protocol version 1
    int protocol = (max_protocol >= 1 && min_protocol <= 1) ? 1 : 0;
    
    if (protocol == 0) {
        Json error = Json::object();
        error["type"] = "hello-error";
        error["error"] = "Unsupported protocol version";
        return error;
    }
    
    client->set_protocol_version(protocol);
    
    // Extract client info
    if (params.contains("client")) {
        Json client_info = params["client"];
        if (client_info.contains("id")) {
            client->set_client_id(client_info.value("id", std::string("")));
        }
    }
    
    // Check authentication
    bool auth_ok = true;
    if (!auth_token_.empty()) {
        if (params.contains("auth")) {
            Json auth = params["auth"];
            std::string token = auth.value("token", std::string(""));
            auth_ok = (token == auth_token_);
        } else {
            auth_ok = false;
        }
    }
    
    client->set_authenticated(auth_ok);
    
    // Build hello-ok response
    Json response = Json::object();
    response["type"] = "hello-ok";
    response["protocol"] = protocol;
    
    Json server_info = Json::object();
    server_info["version"] = "opencrank-cpp-1.0.0";
    server_info["connId"] = client->conn_id();
    server_info["host"] = "localhost";
    response["server"] = server_info;
    
    Json features = Json::object();
    Json methods = Json::array();
    methods.push_back("chat.send");
    methods.push_back("config.get");
    methods.push_back("health.get");
    methods.push_back("models.list");
    features["methods"] = methods;
    
    Json events = Json::array();
    events.push_back("chat.delta");
    events.push_back("chat.done");
    events.push_back("heartbeat");
    features["events"] = events;
    response["features"] = features;
    
    // Initial snapshot
    Json snapshot = Json::object();
    snapshot["config"] = Json::object();
    
    Json health = Json::object();
    health["status"] = "ok";
    health["uptime"] = 0;
    snapshot["health"] = health;
    
    snapshot["models"] = Json::array();
    response["snapshot"] = snapshot;
    
    return response;
}

Json GatewayPlugin::handle_chat_send(GatewayClient* /* client */, const Json& params) {
    Json result = Json::object();
    
    // Only 'text' is required now
    if (!params.contains("text")) {
        result["error"] = "Missing required parameter: text";
        return result;
    }
    
    std::string text = params.value("text", std::string(""));
    std::string reply_to = params.value("reply_to", std::string(""));

    LOG_INFO("Gateway chat.send: text=%s", text.c_str());

    // Check if we have a recent chat to route to
    if (recent_chat_id_.empty()) {
        result["error"] = "No active chat - send a message from a channel first";
        LOG_WARN("[Gateway] Cannot send - no recent chat tracked");
        return result;
    }

    LOG_DEBUG("[Gateway] Routing to recent chat: %s", recent_chat_id_.c_str());

    // Broadcast the message to ALL registered channels
    // Each channel will route to the recent chat_id
    auto& registry = PluginRegistry::instance();
    auto channels = registry.channels();  // Non-const copy
    
    bool any_success = false;
    std::vector<std::string> message_ids;
    std::vector<std::string> errors;
    
    for (auto* channel : channels) {
        if (!channel) continue;
        
        std::string channel_id = channel->channel_id();
        SendResult send_result;
        
        if (!reply_to.empty()) {
            send_result = channel->send_message(recent_chat_id_, text, reply_to);
        } else {
            send_result = channel->send_message(recent_chat_id_, text);
        }
        
        if (send_result.success) {
            any_success = true;
            message_ids.push_back(send_result.message_id);
            LOG_DEBUG("[Gateway] Message sent successfully to %s:%s - msg_id=%s", 
                     channel_id.c_str(), recent_chat_id_.c_str(), send_result.message_id.c_str());
        } else {
            errors.push_back("[" + channel_id + "] " + send_result.error);
            LOG_ERROR("Failed to send message to %s:%s - %s", 
                     channel_id.c_str(), recent_chat_id_.c_str(), send_result.error.c_str());
        }
    }
    
    if (any_success) {
        result["success"] = true;
        result["message_ids"] = message_ids;
        result["recent_chat"] = recent_chat_id_;
    } else {
        result["success"] = false;
        result["error"] = errors;
    }
    
    return result;
}

Json GatewayPlugin::handle_config_get(GatewayClient* /* client */, const Json& /* params */) {
    Json result = Json::object();
    result["config"] = Json::object();
    return result;
}

Json GatewayPlugin::handle_health_get(GatewayClient* /* client */, const Json& /* params */) {
    Json result = Json::object();
    result["status"] = "ok";
    result["clients"] = static_cast<int>(client_count());
    result["port"] = static_cast<int>(port_);
    return result;
}

Json GatewayPlugin::handle_models_list(GatewayClient* /* client */, const Json& /* params */) {
    Json result = Json::object();
    result["models"] = Json::array();
    return result;
}

void GatewayPlugin::route_incoming_message(const Message& msg) {
    // Broadcast incoming message to all connected gateway clients
    Json event_payload = Json::object();
    event_payload["id"] = msg.id;
    event_payload["channel"] = msg.channel;
    event_payload["from"] = msg.from;
    event_payload["from_name"] = msg.from_name;
    event_payload["to"] = msg.to;
    event_payload["text"] = msg.text;
    event_payload["chat_type"] = msg.chat_type;
    event_payload["timestamp"] = static_cast<int>(msg.timestamp);
    
    if (!msg.reply_to_id.empty()) {
        event_payload["reply_to_id"] = msg.reply_to_id;
    }
    if (!msg.media_url.empty()) {
        event_payload["media_url"] = msg.media_url;
    }
    
    LOG_DEBUG("[Gateway] routing incoming message from %s: %s", 
              msg.from_name.c_str(), msg.text.c_str());
    
    broadcast("chat.message", event_payload);
}

void GatewayPlugin::on_incoming_message(const Message& msg) {
    // This is called by the main application for all incoming messages
    
    // Track most recent chat for routing outgoing messages
    recent_chat_id_ = msg.to;
    
    route_incoming_message(msg);
}

void GatewayPlugin::on_typing_indicator(const std::string& channel_id,
                                        const std::string& chat_id,
                                        bool typing) {
    // Forward typing events to WebSocket clients
    send_typing_event(channel_id, chat_id, typing);
}

} // namespace opencrank

// Export plugin for dynamic loading
OPENCRANK_DECLARE_PLUGIN(opencrank::GatewayPlugin, "gateway", "1.0.0", 
                        "WebSocket gateway server (Crow backend)", "service")
