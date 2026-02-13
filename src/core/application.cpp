/*
 * OpenCrank C++ - Application Implementation
 * 
 * Central application singleton managing the lifecycle of all components.
 */
#include <opencrank/core/application.hpp>
#include <opencrank/core/logger.hpp>
#include <opencrank/core/commands.hpp>
#include <opencrank/core/sandbox.hpp>
#include <opencrank/core/builtin_tools.hpp>
#include <opencrank/core/browser_tool.hpp>
#include <opencrank/core/memory_tool.hpp>
#include <opencrank/core/message_handler.hpp>
#include <opencrank/core/utils.hpp>

#include <iostream>
#include <csignal>
#include <cstring>
#include <climits>
#include <unistd.h>
#include <sys/stat.h>
#include <curl/curl.h>

namespace opencrank {

// ============================================================================
// AppInfo Implementation
// ============================================================================

const char* AppInfo::default_system_prompt() {
    return 
        "You are OpenCrank, a helpful AI assistant running on a minimal C++ framework. "
        "You are friendly, concise, and helpful. Keep responses brief unless asked for detail. "
        "You can help with questions, coding, and general conversation.";
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
              << "Example:\n"
              << "  " << prog << " config.json\n";
}

void print_version() {
    std::cout << AppInfo::NAME << " v" << AppInfo::VERSION << " (dynamic plugins)\n";
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
    , thread_pool_(nullptr)
    , user_limiter_(KeyedRateLimiter::TOKEN_BUCKET, 10, 2)
    , debouncer_(5)
    , system_prompt_("")
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

void Application::setup_sandbox() {
    auto& sandbox = Sandbox::instance();
    
    // Phase 1: Initialize sandbox directories (~/.opencrank/db, ~/.opencrank/jail)
    // and override config paths. Landlock is NOT activated yet so that plugins,
    // config files, and shared libraries can still be loaded.
    if (!sandbox.init()) {
        LOG_ERROR("Failed to initialize sandbox directories");
        return;
    }
    
    // Override workspace_dir to point into the jail
    auto configured_workspace = config_.get_string("workspace_dir", "");
    if (configured_workspace.empty() || configured_workspace == ".") {
        config_.set_string("workspace_dir", sandbox.jail_dir());
        LOG_INFO("[Sandbox] workspace_dir -> %s", sandbox.jail_dir().c_str());
    }
    
    // Override memory database path to ~/.opencrank/db/memory.db
    auto configured_db = config_.get_string("memory_db_path", "");
    if (configured_db.empty() || configured_db.find("/.opencrank/") != std::string::npos) {
        config_.set_string("memory_db_path", sandbox.memory_db_path());
        LOG_INFO("[Sandbox] memory_db_path -> %s", sandbox.memory_db_path().c_str());
    }
    
    LOG_INFO("[Sandbox] Directories ready (Landlock will activate after init)");
}

void Application::activate_sandbox() {
    // Phase 2: Activate Landlock. Called AFTER all plugins, configs, and
    // shared libraries have been loaded. From this point on, neither this
    // process nor any child process can access the filesystem outside the
    // allowed paths.
    bool sandbox_enabled = config_.get_bool("sandbox.enabled", true);
    if (!sandbox_enabled) {
        LOG_WARN("[Sandbox] Sandbox disabled by config (sandbox.enabled=false)");
        return;
    }
    
    auto& sandbox = Sandbox::instance();
    if (sandbox.base_dir().empty()) {
        LOG_WARN("[Sandbox] Sandbox not initialized, skipping activation");
        return;
    }
    
    if (sandbox.activate()) {
        LOG_INFO("[Sandbox] Process jailed into %s", sandbox.base_dir().c_str());
    } else {
        LOG_WARN("[Sandbox] Could not activate Landlock sandbox.");
        LOG_WARN("[Sandbox] The process is NOT sandboxed. Consider upgrading to Linux >= 5.13.");
    }
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
    
    LOG_DEBUG("Built %zu skill command specs", skill_command_specs_.size());
    for (const auto& spec : skill_command_specs_) {
        LOG_DEBUG("  /%s -> skill '%s' (%s)", spec.name.c_str(), spec.skill_name.c_str(), spec.description.c_str());
    }
}

void Application::setup_system_prompt() {
    // ── 1. Default base prompt ──
    system_prompt_ = AppInfo::default_system_prompt();
    LOG_DEBUG("Loaded default system prompt");

    // ── 2. Custom prompt from config.json ──
    auto custom_prompt = config_.get_string("system_prompt", "");
    if (!custom_prompt.empty()) {
        system_prompt_ += "\n\n";
        system_prompt_ += custom_prompt;
        LOG_DEBUG("Appended custom system prompt from config");
    }

    // ── 3. Skills section (from already-loaded skill entries) ──
    if (!skill_entries_.empty()) {
        // Reload entries to pass to build_skills_section
        auto entries = skill_manager_.load_workspace_skill_entries();
        auto skills_section = skill_manager_.build_skills_section(&entries);
        if (!skills_section.empty()) {
            system_prompt_ += "\n\n";
            system_prompt_ += skills_section;
            LOG_DEBUG("Appended skills section to system prompt");
        }
    }

    // ── Summary ──
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

void Application::setup_agent() {
    LOG_INFO("Initializing agent tools...");
    
    // Load agent configuration from config file
    AgentConfig agent_config;
    agent_config.max_iterations = static_cast<int>(
        config_.get_int("agent.max_iterations", 15));
    agent_config.max_consecutive_errors = static_cast<int>(
        config_.get_int("agent.max_consecutive_errors", 5));
    agent_config.max_tool_result_size = static_cast<size_t>(
        config_.get_int("agent.max_tool_result_size", 15000));
    agent_config.auto_chunk_large_results = 
        config_.get_bool("agent.auto_chunk_large_results", true);
    agent_config.chunk_size = static_cast<size_t>(
        config_.get_int("agent.chunk_size", 0));
    
    // Try to get context_size from AI provider configs (llamacpp or claude)
    int64_t ctx = config_.get_int("llamacpp.context_size", 0);
    if (ctx == 0) {
        ctx = config_.get_int("claude.context_size", 0);
    }
    agent_config.context_size = static_cast<size_t>(ctx);
    
    agent_.set_config(agent_config);
    
    LOG_INFO("Agent config: max_iterations=%d, max_consecutive_errors=%d, "
             "max_tool_result_size=%zu, chunk_size=%zu (effective=%zu), context_size=%zu tokens",
             agent_config.max_iterations, agent_config.max_consecutive_errors,
             agent_config.max_tool_result_size, agent_config.chunk_size,
             agent_config.effective_chunk_size(), agent_config.context_size);
    
    // Built-in tools are now registered via BuiltinToolsProvider
    // which will be initialized along with other plugins in setup_plugins()
    
    LOG_INFO("Agent ready (tools will be registered from providers)");
}

void Application::setup_plugins() {
    // Configure plugin search path from config, defaulting to ~/.opencrank/plugins
    auto plugins_dir = config_.get_string("plugins_dir", "");

    if (plugins_dir.empty()) {
        // Default to sandbox-friendly location
        const char* home = getenv("HOME");
        if (home) {
            plugins_dir = std::string(home) + "/.opencrank/plugins";
        } else {
            plugins_dir = ".opencrank/plugins";
        }
        LOG_INFO("Using default plugins directory: %s", plugins_dir.c_str());
    }
    
    loader_.add_search_path(plugins_dir);
    
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
        LOG_WARN("No AI provider configured. Set in config.json to enable AI features.");
    }
    
    LOG_INFO("%d channel(s) started, ready to receive messages", started_count);
}

void Application::warmup_ai() {
    auto* ai = registry().get_default_ai();
    if (!ai || !ai->is_configured()) {
        LOG_DEBUG("Skipping AI warmup - no AI configured");
        return;
    }
    
    LOG_INFO("Warming up AI connection...");
    
    // Send a minimal warmup message to establish connection
    std::vector<ConversationMessage> warmup_history;
    warmup_history.push_back(ConversationMessage::system("You are a helpful AI assistant."));
    warmup_history.push_back(ConversationMessage::user("Hello"));
    
    CompletionOptions opts;
    opts.max_tokens = 10;  // Very short response
    opts.temperature = 0.1; // Low creativity for fast response
    
    CompletionResult result = ai->chat(warmup_history, opts);
    
    if (result.success) {
        LOG_INFO("AI warmup successful - connection established");
    } else {
        LOG_WARN("AI warmup failed: %s", result.error.c_str());
    }
}

bool Application::init(int argc, char* argv[]) {
    // Initialize libcurl globally (before threads start)
    curl_global_init(CURL_GLOBAL_ALL);

    // Create thread pool after curl init
    thread_pool_ = new ThreadPool(8);

    // Change to home directory for consistent path resolution
    {
        const char* home = getenv("HOME");
        std::string app_dir;
        if (home && home[0] != '\0') {
            app_dir = std::string(home) + "/.opencrank";
        } else {
            app_dir = ".opencrank";
        }
        
        // Create the directory if it doesn't exist
        mkdir(app_dir.c_str(), 0755);
        
        // Best-effort change; ignore error here
        chdir(app_dir.c_str());
    }
    
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

    setup_plugins();

    // Setup sandbox (must be before any AI/tool processing)
    setup_sandbox();
    
    // Setup components in order
    setup_logging();
    setup_channels();
    setup_skills();
    setup_system_prompt();
    setup_agent();
    
    // Configure session manager
    sessions().set_max_history(static_cast<size_t>(
        config_.get_int("session.max_history", 20)));
    
    // Warm up AI connection
    warmup_ai();
    
    // Start AI process monitor
    AIProcessMonitor::Config monitor_config;
    monitor_config.hang_timeout_seconds = config_.get_int("ai_monitor.hang_timeout", 30);
    monitor_config.typing_interval_seconds = config_.get_int("ai_monitor.typing_interval", 3);
    monitor_config.check_interval_ms = config_.get_int("ai_monitor.check_interval_ms", 5000);
    ai_monitor_.set_config(monitor_config);
    
    // Set hung session callback
    ai_monitor_.set_hung_callback([](const std::string& session_id, int elapsed_seconds) {
        LOG_ERROR("AI HUNG DETECTED: session [%s] no heartbeat for %d seconds",
                  session_id.c_str(), elapsed_seconds);
        // Future: could implement recovery actions here
    });
    
    ai_monitor_.start();
    LOG_INFO("AI process monitor started");
    
    // NOW activate Landlock sandbox - all plugins, configs, and shared
    // libraries are loaded. From here on, filesystem is locked down.
    activate_sandbox();
    
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

int Application::run() {
    LOG_INFO("Entering main loop (poll interval: 100ms)");
    LOG_DEBUG("[App] Active channels: %zu, Active plugins: %zu, Agent tools: %zu",
              registry().channels().size(), registry().plugins().size(), agent_.tools().size());
    
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
    LOG_DEBUG("[App] AI monitor stopped");
    
    // Stop thread pool (wait for pending)
    if (thread_pool_) {
        LOG_DEBUG("[App] Stopping thread pool (pending: %zu)", thread_pool_->pending());
        thread_pool_->shutdown();
        delete thread_pool_;
        thread_pool_ = nullptr;
        LOG_DEBUG("[App] Thread pool stopped");
    }
    
    registry().stop_all_channels();
    registry().shutdown_all();
    loader_.unload_all();
    
    // Cleanup libcurl
    curl_global_cleanup();
    
    LOG_INFO("Goodbye!");
}

} // namespace opencrank
