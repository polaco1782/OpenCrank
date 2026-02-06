/*
 * OpenCrank C++11 - Application Implementation
 * 
 * Central application singleton managing the lifecycle of all components.
 */
#include <opencrank/core/application.hpp>
#include <opencrank/core/logger.hpp>
#include <opencrank/core/commands.hpp>
#include <opencrank/core/builtin_tools.hpp>
#include <opencrank/core/browser_tool.hpp>
#include <opencrank/core/memory_tool.hpp>
#include <opencrank/core/message_handler.hpp>
#include <opencrank/core/utils.hpp>

#include <iostream>
#include <csignal>
#include <cstring>
#include <unistd.h>
#include <curl/curl.h>

namespace opencrank {

// ============================================================================
// AppInfo Implementation
// ============================================================================

const char* AppInfo::default_system_prompt() {
    return 
        "You are OpenCrank, a helpful AI assistant running on a minimal C++11 framework. "
        "You are friendly, concise, and helpful. Keep responses brief unless asked for detail. "
        "You can help with questions, coding, and general conversation.\n\n"
        "You have access to tools that let you browse the web and manage memory/tasks.";
}

// ============================================================================
// Utility Functions
// ============================================================================

void print_usage(const char* prog) {
    std::cout << AppInfo::NAME << " - Personal AI Assistant Framework\n\n"
              << "Usage: " << prog << " [options] [config.json]\n\n"
              << "Options:\n"
              << "  -h, --help     Show this help message\n"
              << "  -v, --version  Show version\n\n"
              << "Config file format (JSON):\n"
              << "  {\n"
              << "    \"plugins\": [\"telegram\", \"claude\", \"browser\", \"memory\"],\n"
              << "    \"plugins_dir\": \"./plugins\",\n"
              << "    \"log_level\": \"info\",\n"
              << "    \"system_prompt\": \"You are a helpful assistant.\",\n"
              << "    \"telegram\": { \"bot_token\": \"...\" },\n"
              << "    \"claude\": { \"api_key\": \"...\" }\n"
              << "  }\n\n"
              << "Example:\n"
              << "  " << prog << " config.json\n";
}

void print_version() {
    std::cout << AppInfo::NAME << " v" << AppInfo::VERSION << " (dynamic plugins)\n"
              << "Available plugins: telegram, whatsapp, browser, claude, memory\n";
}

std::vector<std::string> split_message_chunks(const std::string& text, size_t max_len) {
    std::vector<std::string> chunks;
    
    if (text.size() <= max_len) {
        chunks.push_back(text);
        return chunks;
    }

    size_t start = 0;
    while (start < text.size()) {
        size_t remaining = text.size() - start;
        if (remaining <= max_len) {
            chunks.push_back(text.substr(start));
            break;
        }

        size_t end = start + max_len;
        
        // Try to split at newline for cleaner breaks
        size_t split_pos = text.rfind('\n', end);
        if (split_pos == std::string::npos || split_pos <= start) {
            // No good newline, use truncate_safe for clean UTF-8 cut
            std::string slice = text.substr(start, max_len);
            slice = truncate_safe(slice, max_len);
            if (slice.empty()) {
                break;
            }
            chunks.push_back(slice);
            start += slice.size();
        } else {
            chunks.push_back(text.substr(start, split_pos - start));
            start = split_pos + 1;
        }

        // Skip consecutive newlines
        while (start < text.size() && text[start] == '\n') {
            ++start;
        }
    }

    return chunks;
}

// ============================================================================
// Signal Handler
// ============================================================================

namespace {
    void signal_handler(int sig) {
        (void)sig;
        Application::instance().stop();
        LOG_INFO("Received shutdown signal");
    }
}

// ============================================================================
// Application Implementation
// ============================================================================

Application& Application::instance() {
    static Application app;
    return app;
}

Application::Application() 
    : running_(true)
    , thread_pool_(8)  // 8 worker threads
    , user_limiter_(KeyedRateLimiter::TOKEN_BUCKET, 10, 2)
    , debouncer_(5)
    , system_prompt_(AppInfo::default_system_prompt())
    , config_file_("config.json")
{}

bool Application::parse_args(int argc, char* argv[]) {

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return false;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return false;
        }
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_file_ = std::string(argv[++i]);
            continue;
        }
    }
    return true;
}

void Application::setup_logging() {
    auto log_level = config_.get_string("log_level", "info");
    
    if (log_level == "debug") {
        Logger::instance().set_level(LogLevel::DEBUG);
    } else if (log_level == "warn") {
        Logger::instance().set_level(LogLevel::WARN);
    } else if (log_level == "error") {
        Logger::instance().set_level(LogLevel::ERROR);
    }
    
    // Load custom system prompt from config
    auto custom_prompt = config_.get_string("system_prompt", "");
    if (!custom_prompt.empty()) {
        system_prompt_ = custom_prompt;
        LOG_DEBUG("Using custom system prompt from config");
    }
}

void Application::setup_skills() {
    LOG_INFO("Initializing skills system...");
    
    SkillsConfig skills_config;
    skills_config.workspace_dir = config_.get_string("workspace_dir", ".");
    skills_config.bundled_skills_dir = config_.get_string("skills.bundled_dir", "");
    skills_config.managed_skills_dir = config_.get_string("skills.managed_dir", "");
    
    skill_manager_.set_config(skills_config);
    
    // Load and filter skills
    auto entries = skill_manager_.load_workspace_skill_entries();
    auto eligible = skill_manager_.filter_skill_entries(entries, nullptr);
    
    LOG_INFO("Loaded %zu skills (%zu eligible for this environment)", 
             entries.size(), eligible.size());
    
    // Store for later use
    skill_entries_ = std::move(eligible);
    skill_command_specs_ = skill_manager_.build_workspace_skill_command_specs(&entries, nullptr, nullptr);
    
    LOG_DEBUG("[Skills] Built %zu skill command specs", skill_command_specs_.size());
    for (const auto& spec : skill_command_specs_) {
        LOG_DEBUG("[Skills]   /%s -> skill '%s' (%s)", spec.name.c_str(), spec.skill_name.c_str(), spec.description.c_str());
    }
    
    // Append skills section to system prompt
    if (!skill_entries_.empty()) {
        auto skills_section = skill_manager_.build_skills_section(&entries);
        if (!skills_section.empty()) {
            system_prompt_ += "\n\n" + skills_section;
            LOG_DEBUG("Appended skills section to system prompt");
            
            auto prompt_size = system_prompt_.size();
            if (prompt_size > 20000) {
                LOG_WARN("System prompt is very large (%zu chars). This may consume significant context window.",
                         prompt_size);
            } else if (prompt_size > 10000) {
                LOG_WARN("System prompt is large (%zu chars).", prompt_size);
            }
            LOG_DEBUG("Final system prompt size: %zu characters (~%zu tokens)", 
                      prompt_size, prompt_size / 4);
        }
    }
}

void Application::setup_agent() {
    LOG_INFO("Initializing agent tools...");
    
    // Built-in tools are now registered via BuiltinToolsProvider
    // which will be initialized along with other plugins in setup_plugins()
    
    LOG_INFO("Agent ready (tools will be registered from providers)");
}

void Application::setup_plugins() {
    // Configure plugin search paths
    loader_.add_search_path("./bin/plugins");
    loader_.add_search_path("./plugins");
    
    auto plugins_dir = config_.get_string("plugins_dir", "");
    if (!plugins_dir.empty()) {
        loader_.add_search_path(plugins_dir);
    }
    
    // Load external plugins from config
    int loaded = loader_.load_from_config(config_);
    LOG_INFO("Loaded %d external plugins", loaded);
    
    // Register internal/core tool providers (built into the binary)
    static BuiltinToolsProvider builtin_tools_provider;
    static BrowserTool browser_tool;
    static MemoryTool memory_tool;
    
    registry().register_plugin(&builtin_tools_provider);
    registry().register_plugin(&browser_tool);
    registry().register_plugin(&memory_tool);
    LOG_DEBUG("Registered 3 core tool providers (builtin, browser, memory)");
    
    // Register external plugins with registry
    for (const auto& plugin : loader_.plugins()) {
        if (plugin.instance) {
            registry().register_plugin(plugin.instance);
            LOG_DEBUG("Registered external plugin: %s (%s)", 
                      plugin.info.name, plugin.info.type);
        }
    }

    LOG_INFO("Registered %zu plugins (%zu channels, %zu tools, %zu AI providers)",
             registry().plugins().size(), 
             registry().channels().size(),
             registry().tools().size(),
             registry().ai_providers().size());
    
    // Register core commands
    register_core_commands(config_, registry());

    // Initialize all plugins
    registry().init_all(config_);
    
    LOG_INFO("Registered %zu commands", registry().commands().size());

    // Set up chunker reference for builtin tools
    builtin_tools_provider.set_chunker(&agent_.chunker());

    // Register tool plugins with the agent
    for (auto* tool_plugin : registry().tools()) {
        if (!tool_plugin || !tool_plugin->is_initialized()) {
            continue;
        }

        auto agent_tools = tool_plugin->get_agent_tools();
        for (const auto& tool : agent_tools) {
            agent_.register_tool(tool);
            LOG_DEBUG("Registered agent tool: %s", tool.name.c_str());
        }
    }

    LOG_INFO("Registered %zu total agent tools", agent_.tools().size());
}

void Application::setup_channels() {
    auto& channels = registry().channels();
    
    // Set callbacks
    for (auto* channel : channels) {
        if (channel->is_initialized()) {
            channel->set_message_callback(on_message);
            channel->set_error_callback(on_error);
        }
    }
    
    // Start channels
    int started_count = 0;
    for (auto* channel : channels) {
        if (channel->is_initialized() && channel->start()) {
            LOG_INFO("Started channel: %s", channel->channel_id());
            started_count++;
        }
    }
    
    // Check gateway
    auto* gateway = registry().get_plugin("gateway");
    bool has_gateway = (gateway != nullptr && gateway->is_initialized());
    
    if (started_count == 0 && !has_gateway) {
        LOG_ERROR("No channels or gateway started. Configure at least one:");
        LOG_ERROR("  1. Set telegram.bot_token in config.json for Telegram");
        LOG_ERROR("  2. Or enable gateway with gateway.port in config.json");
    }
    
    if (has_gateway) {
        LOG_INFO("Gateway service available - will start on first poll");
    }
    
    // Log AI status
    auto* ai = registry().get_default_ai();
    if (ai && ai->is_configured()) {
        LOG_INFO("AI provider: %s (%s)", ai->provider_id().c_str(), ai->default_model().c_str());
    } else {
        LOG_WARN("No AI provider configured. Set claude.api_key in config.json for Claude.");
    }
    
    LOG_INFO("%d channel(s) started, ready to receive messages", started_count);
}

bool Application::init(int argc, char* argv[]) {
    // Initialize libcurl globally (before threads start)
    curl_global_init(CURL_GLOBAL_ALL);
    
    // Parse command line
    if (!parse_args(argc, argv)) {
        return false;
    }
   
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    LOG_INFO("%s v%s starting...", AppInfo::NAME, AppInfo::VERSION);
    
    // Load configuration
    if (!config_.load_file(config_file_)) {
        LOG_WARN("Failed to load config from %s, aborting!", config_file_.c_str());
        return false;
    } else {
        LOG_INFO("Loaded config from %s", config_file_.c_str());
    }
        
    // Setup components in order
    setup_logging();
    setup_skills();
    
    // Configure session manager
    sessions().set_max_history(static_cast<size_t>(
        config_.get_int("session.max_history", 20)));
    
    setup_agent();
    setup_plugins();
    setup_channels();
    
    // Start AI process monitor
    AIProcessMonitor::Config monitor_config;
    monitor_config.hang_timeout_seconds = config_.get_int("ai_monitor.hang_timeout", 30);
    monitor_config.typing_interval_seconds = config_.get_int("ai_monitor.typing_interval", 3);
    monitor_config.check_interval_ms = config_.get_int("ai_monitor.check_interval_ms", 1000);
    ai_monitor_.set_config(monitor_config);
    
    // Set hung session callback
    ai_monitor_.set_hung_callback([](const std::string& session_id, int elapsed_seconds) {
        LOG_ERROR("AI HUNG DETECTED: session [%s] no heartbeat for %d seconds",
                  session_id.c_str(), elapsed_seconds);
        // Future: could implement recovery actions here
    });
    
    ai_monitor_.start();
    LOG_INFO("AI process monitor started");
    
    // Verify we have something to run
    auto* gateway = registry().get_plugin("gateway");
    bool has_gateway = (gateway != nullptr && gateway->is_initialized());
    
    int channel_count = 0;
    for (auto* ch : registry().channels()) {
        if (ch->is_initialized()) channel_count++;
    }
    
    if (channel_count == 0 && !has_gateway) {
        return false;
    }
    
    return true;
}

void Application::rebuild_system_prompt() {
    // Rebuild with current skills
    auto base_prompt = config_.get_string("system_prompt", AppInfo::default_system_prompt());
    system_prompt_ = base_prompt;
    
    auto entries = skill_manager_.load_workspace_skill_entries();
    auto eligible = skill_manager_.filter_skill_entries(entries, nullptr);
    
    if (!eligible.empty()) {
        auto snapshot = skill_manager_.build_workspace_skill_snapshot(&entries, nullptr);
        if (!snapshot.prompt.empty()) {
            system_prompt_ += "\n\n" + snapshot.prompt;
        }
    }
    
    LOG_DEBUG("System prompt rebuilt with %zu eligible skills", eligible.size());
}

int Application::run() {
    LOG_INFO("Entering main loop");
    
    while (running_.load()) {
        // Poll all plugins
        registry().poll_all();
        sleep_ms(100);
        
        // Periodic cleanup (~10 seconds)
        static int cleanup_counter = 0;
        if (++cleanup_counter >= 100) {
            cleanup_counter = 0;
            sessions().cleanup_inactive(3600);  // 1 hour timeout
            user_limiter_.cleanup(3600);
        }
    }
    
    return 0;
}

void Application::shutdown() {
    LOG_INFO("Shutting down...");
    
    // Stop AI monitor first
    ai_monitor_.stop();
    
    // Stop thread pool (wait for pending)
    thread_pool_.shutdown();
    
    registry().stop_all_channels();
    registry().shutdown_all();
    loader_.unload_all();
    
    // Cleanup libcurl
    curl_global_cleanup();
    
    LOG_INFO("Goodbye!");
}

} // namespace opencrank
