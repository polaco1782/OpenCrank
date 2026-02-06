#ifndef OPENCRANK_CORE_COMMANDS_HPP
#define OPENCRANK_CORE_COMMANDS_HPP

#include <opencrank/core/types.hpp>
#include <opencrank/core/config.hpp>
#include <opencrank/core/registry.hpp>
#include <opencrank/core/session.hpp>
#include <string>

namespace opencrank {

namespace commands {
    std::string cmd_ping(const Message& msg, Session& session, const std::string& args);
    std::string cmd_help(const Message& msg, Session& session, const std::string& args);
    std::string cmd_info(const Message& msg, Session& session, const std::string& args);
    std::string cmd_start(const Message& msg, Session& session, const std::string& args);
    std::string cmd_new(const Message& msg, Session& session, const std::string& args);
    std::string cmd_status(const Message& msg, Session& session, const std::string& args);
    std::string cmd_tools(const Message& msg, Session& session, const std::string& args);
    std::string cmd_monitor(const Message& msg, Session& session, const std::string& args);
}

// Register built-in core commands (/ping, /help, /info, /start, /new, /status, /tools)
void register_core_commands(const Config& cfg, PluginRegistry& registry);

} // namespace opencrank

#endif // OPENCRANK_CORE_COMMANDS_HPP
