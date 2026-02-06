#ifndef opencrank_PLUGIN_PLUGIN_HPP
#define opencrank_PLUGIN_PLUGIN_HPP

#include "../core/config.hpp"
#include "../core/types.hpp"
#include <string>

namespace opencrank {

// Base plugin interface
class Plugin {
public:
    virtual ~Plugin() {}
    
    // Plugin metadata
    virtual const char* name() const = 0;
    virtual const char* version() const = 0;
    virtual const char* description() const { return ""; }
    
    // Lifecycle
    virtual bool init(const Config& cfg) = 0;
    virtual void shutdown() = 0;
    
    // Plugin state
    virtual bool is_initialized() const { return initialized_; }
    
    // Optional: plugins can override this for periodic updates
    virtual void poll() {}
    
    // Optional: plugins can override this to receive all incoming messages
    // (useful for gateway/logging plugins that need to see all traffic)
    virtual void on_incoming_message(const Message& /* msg */) {}
    
    // Optional: plugins can override this to receive typing indicator events
    // (useful for gateway plugins that want to show typing status)
    virtual void on_typing_indicator(const std::string& /* channel_id */,
                                     const std::string& /* chat_id */,
                                     bool /* typing */) {}
    
protected:
    bool initialized_;
    
    Plugin() : initialized_(false) {}
};

} // namespace opencrank

#endif // opencrank_PLUGIN_PLUGIN_HPP
