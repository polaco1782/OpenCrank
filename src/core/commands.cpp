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
    cmds.push_back(CommandDef("/continue", "Resume paused agent task", commands::cmd_continue));
    cmds.push_back(CommandDef("/cancel", "Cancel paused agent task", commands::cmd_cancel));

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
           "I'm a personal AI assistant powered by C++. I can chat with you, run tools, and help automate tasks.\n\n"
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
    
    // Show paused task info if applicable
    if (session.has_data("agent_paused")) {
        oss << "\n\n⏸️ **Paused Task**\n";
        if (session.has_data("agent_iterations")) {
            oss << "Iterations completed: " << session.get_data("agent_iterations") << "\n";
        }
        if (session.has_data("agent_tool_calls")) {
            oss << "Tool calls made: " << session.get_data("agent_tool_calls") << "\n";
        }
        oss << "\nUse `/continue` to resume or `/cancel` to cancel.";
    }
    
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

std::string cmd_continue(const Message& msg, Session& session, const std::string& args) {
    // Check if there's a paused agent task
    if (!session.has_data("agent_paused")) {
        return "⚠️ No paused task to continue. Use this command after a task is paused at max iterations.";
    }
    
    Application& app = Application::instance();
    auto* ai = app.registry().get_default_ai();
    if (!ai || !ai->is_configured()) {
        return "⚠️ AI not configured.";
    }
    
    // Parse arguments for iteration limit
    int additional_iterations = 15;  // Default
    bool no_stop = false;
    
    std::string args_trimmed = trim(args);
    if (!args_trimmed.empty()) {
        if (args_trimmed == "no-stop" || args_trimmed == "nostop") {
            no_stop = true;
            additional_iterations = 999;  // Very high limit
            LOG_INFO("[continue] User requested no-stop mode (999 iterations)");
        } else {
            // Try to parse as number
            try {
                additional_iterations = std::stoi(args_trimmed);
                if (additional_iterations < 1) additional_iterations = 15;
                if (additional_iterations > 999) additional_iterations = 999;
                LOG_INFO("[continue] User requested %d additional iterations", additional_iterations);
            } catch (...) {
                return "⚠️ Invalid argument. Usage: `/continue`, `/continue <N>`, or `/continue no-stop`";
            }
        }
    }
    
    // Get previous stats
    int prev_iterations = 0;
    int prev_tool_calls = 0;
    if (session.has_data("agent_iterations")) {
        prev_iterations = std::stoi(session.get_data("agent_iterations", "0"));
    }
    if (session.has_data("agent_tool_calls")) {
        prev_tool_calls = std::stoi(session.get_data("agent_tool_calls", "0"));
    }
    
    // Clear paused state
    session.remove_data("agent_paused");
    session.remove_data("agent_iterations");
    session.remove_data("agent_tool_calls");
    
    // Build continuation prompt
    std::string continuation_msg = "Continue with the task. The iteration limit has been ";
    if (no_stop) {
        continuation_msg += "removed. Please complete the task.";
    } else {
        continuation_msg += "increased. You have " + std::to_string(additional_iterations) + " more iterations.";
    }
    
    // Start AI monitoring
    std::string monitor_session_id = msg.channel + ":" + msg.to;
    app.ai_monitor().start_session(monitor_session_id, msg.channel, msg.to);
    app.typing().start_typing(msg.to);
    
    LOG_INFO("[continue] Resuming agent loop with %d more iterations (prev: %d iterations, %d tool calls)",
             additional_iterations, prev_iterations, prev_tool_calls);
    
    // Configure agent with new limit
    AgentConfig agent_config;
    agent_config.max_iterations = additional_iterations;
    agent_config.max_consecutive_errors = 3;
    
    // Run agent loop with continuation message
    auto agent_result = app.agent().run(
        ai,
        continuation_msg,
        session.history(),
        app.system_prompt(),
        agent_config
    );
    
    app.typing().stop_typing(msg.to);
    app.ai_monitor().end_session(monitor_session_id);
    
    std::string response;
    if (agent_result.paused) {
        // Paused again - store updated state
        session.set_data("agent_paused", "true");
        session.set_data("agent_iterations", std::to_string(prev_iterations + agent_result.iterations));
        session.set_data("agent_tool_calls", std::to_string(prev_tool_calls + agent_result.tool_calls_made));
        
        response = agent_result.pause_message;
        LOG_INFO("[continue] Agent paused again after %d more iterations (total: %d iterations, %d tool calls)",
                 agent_result.iterations, prev_iterations + agent_result.iterations,
                 prev_tool_calls + agent_result.tool_calls_made);
    } else if (agent_result.success) {
        response = "✅ Task completed!\n\n" + agent_result.final_response;
        LOG_INFO("[continue] Task completed after %d more iterations (total: %d iterations, %d tool calls)",
                 agent_result.iterations, prev_iterations + agent_result.iterations,
                 prev_tool_calls + agent_result.tool_calls_made);
    } else {
        response = "❌ Task failed: " + agent_result.error;
        LOG_ERROR("[continue] Task failed: %s", agent_result.error.c_str());
    }
    
    return response;
}

std::string cmd_cancel(const Message& /*msg*/, Session& session, const std::string& /*args*/) {
    if (!session.has_data("agent_paused")) {
        return "⚠️ No paused task to cancel.";
    }
    
    // Clear paused state
    session.remove_data("agent_paused");
    session.remove_data("agent_iterations");
    session.remove_data("agent_tool_calls");
    
    LOG_INFO("[cancel] User cancelled paused agent task");
    return "🛑 Paused task cancelled. You can start a new conversation.";
}

} // namespace commands

} // namespace opencrank
