/*
 * OpenCrank C++11 - Core Commands Implementation
 */
#include <opencrank/core/commands.hpp>
#include <opencrank/core/logger.hpp>
#include <opencrank/core/utils.hpp>
#include <opencrank/core/application.hpp>
#include <sstream>

namespace opencrank {

namespace {
static std::string core_app_name = "OpenCrank C++11";
static std::string core_app_version = "0.5.0";
}

void register_core_commands(const Config& cfg, PluginRegistry& registry) {
    // Allow customizing app name/version from config
    std::string name = cfg.get_string("bot.app_name", "");
    if (!name.empty()) core_app_name = name;

    std::string ver = cfg.get_string("bot.app_version", "");
    if (!ver.empty()) core_app_version = ver;

    std::vector<CommandDef> cmds;
    cmds.push_back(CommandDef("/ping", "Check if bot is alive", commands::cmd_ping));
    cmds.push_back(CommandDef("/help", "Show help message", commands::cmd_help));
    cmds.push_back(CommandDef("/info", "Show bot info", commands::cmd_info));
    cmds.push_back(CommandDef("/start", "Welcome message", commands::cmd_start));
    cmds.push_back(CommandDef("/new", "Clear conversation", commands::cmd_new));
    cmds.push_back(CommandDef("/status", "Show session status", commands::cmd_status));
    cmds.push_back(CommandDef("/tools", "List available tools", commands::cmd_tools));
    cmds.push_back(CommandDef("/monitor", "AI monitor status", commands::cmd_monitor));

    registry.register_commands(cmds);
    LOG_INFO("Core commands registered: %zu", cmds.size());
}

// ============================================================================
// Command Implementations
// ============================================================================

namespace commands {

std::string cmd_ping(const Message& /*msg*/, Session& /*session*/, const std::string& /*args*/) {
    return "Pong! 🏓";
}

std::string cmd_help(const Message& /*msg*/, Session& /*session*/, const std::string& /*args*/) {
    PluginRegistry& registry = PluginRegistry::instance();
    const std::map<std::string, CommandDef>& commands = registry.commands();

    std::ostringstream oss;
    oss << "OpenCrank C++11 Bot 🦞\n\n"
        << "Commands:\n";

    for (std::map<std::string, CommandDef>::const_iterator it = commands.begin();
         it != commands.end(); ++it) {
        oss << it->first << " - " << it->second.description << "\n";
    }

    oss << "/skills - List available skills\n"
        << "/skill <name> <args> - Run a skill (or /<skillname> <args>)\n";

    oss << "\nOr just send a message to chat with AI!";
    return oss.str();
}

std::string cmd_info(const Message& msg, Session& /*session*/, const std::string& /*args*/) {
    Application& app = Application::instance();
    PluginRegistry& registry = PluginRegistry::instance();
    SessionManager& sessions = SessionManager::instance();

    AIPlugin* ai = registry.get_default_ai();
    std::string ai_info = ai ?
        (ai->provider_id() + "/" + ai->default_model()) : "not configured";

    // Build channels list
    std::ostringstream channels_list;
    const std::vector<ChannelPlugin*>& channels = registry.channels();
    for (size_t i = 0; i < channels.size(); ++i) {
        if (i > 0) channels_list << ", ";
        channels_list << channels[i]->channel_id();
    }

    // Build tools list from agent (includes all registered tools)
    std::ostringstream tools_list;
    const std::map<std::string, AgentTool>& tools = app.agent().tools();
    size_t count = 0;
    for (std::map<std::string, AgentTool>::const_iterator it = tools.begin();
         it != tools.end(); ++it) {
        if (count > 0) tools_list << ", ";
        tools_list << it->first;
        ++count;
    }

    std::ostringstream oss;
    oss << core_app_name << " v" << core_app_version << "\n"
        << "Channels: " << (channels_list.str().empty() ? "none" : channels_list.str()) << "\n"
        << "Tools: " << tools.size() << " (" << (tools_list.str().empty() ? "none" : tools_list.str()) << ")\n"
        << "AI: " << ai_info << "\n"
        << "Your channel: " << msg.channel << "\n"
        << "Plugins loaded: " << registry.plugins().size() << "\n"
        << "Active sessions: " << sessions.session_count();
    return oss.str();
}

std::string cmd_start(const Message& /*msg*/, Session& /*session*/, const std::string& /*args*/) {
    return "Welcome to OpenCrank! 🦞\n\n"
           "I'm a personal AI assistant powered by Claude.\n"
           "Just send me a message to chat, or type /help for commands.";
}

std::string cmd_new(const Message& /*msg*/, Session& session, const std::string& /*args*/) {
    session.clear_history();
    return "🔄 Conversation cleared. Let's start fresh!";
}

std::string cmd_status(const Message& /*msg*/, Session& session, const std::string& /*args*/) {
    SessionManager& sessions = SessionManager::instance();

    std::ostringstream oss;
    oss << "📊 Session Status\n\n"
        << "Session: " << session.key() << "\n"
        << "Messages: " << session.history().size() << "\n"
        << "Last active: " << format_timestamp(session.last_activity()) << "\n"
        << "Total sessions: " << sessions.session_count();
    return oss.str();
}

std::string cmd_tools(const Message& /*msg*/, Session& /*session*/, const std::string& /*args*/) {
    Application& app = Application::instance();
    const std::map<std::string, AgentTool>& tools = app.agent().tools();

    if (tools.empty()) {
        return "No tools available.";
    }

    std::ostringstream oss;
    oss << "🔧 Available Tools\n\n";

    for (std::map<std::string, AgentTool>::const_iterator it = tools.begin();
         it != tools.end(); ++it) {
        const AgentTool& tool = it->second;
        oss << "**" << tool.name << "**\n";
        oss << "  " << tool.description << "\n";
        
        if (!tool.params.empty()) {
            oss << "  Parameters:\n";
            for (size_t i = 0; i < tool.params.size(); ++i) {
                const ToolParamSchema& param = tool.params[i];
                oss << "    • `" << param.name << "` (" << param.type;
                if (param.required) oss << ", required";
                oss << "): " << param.description << "\n";
            }
        }
        oss << "\n";
    }

    return oss.str();
}

std::string cmd_monitor(const Message& /*msg*/, Session& /*session*/, const std::string& /*args*/) {
    Application& app = Application::instance();
    AIProcessMonitor::Stats stats = app.ai_monitor().get_stats();
    
    std::ostringstream oss;
    oss << "🔍 AI Process Monitor\n\n"
        << "Active sessions: " << stats.active_sessions << "\n"
        << "Total sessions: " << stats.total_sessions_started << "\n"
        << "Hung detected: " << stats.total_hung_detected << "\n"
        << "Typing indicators sent: " << stats.total_typing_indicators_sent << "\n\n"
        << "Config:\n"
        << "  Hang timeout: " << app.ai_monitor().get_config().hang_timeout_seconds << "s\n"
        << "  Typing interval: " << app.ai_monitor().get_config().typing_interval_seconds << "s";
    
    return oss.str();
}

} // namespace commands

} // namespace opencrank
