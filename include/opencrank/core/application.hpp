/*
 * opencrank C++11 - Application Class
 * 
 * Central application singleton managing configuration, plugins, 
 * sessions, and the main event loop.
 */
#ifndef opencrank_CORE_APPLICATION_HPP
#define opencrank_CORE_APPLICATION_HPP

#include "config.hpp"
#include "loader.hpp"
#include "registry.hpp"
#include "sandbox.hpp"
#include "session.hpp"
#include "thread_pool.hpp"
#include "rate_limiter.hpp"
#include "agent.hpp"
#include "ai_monitor.hpp"
#include "../skills/manager.hpp"
#include "../skills/types.hpp"

#include <string>
#include <vector>
#include <atomic>

namespace opencrank {

// ============================================================================
// Application Configuration Constants
// ============================================================================

struct AppInfo {
    static constexpr const char* VERSION = "0.5.0";
    static constexpr const char* NAME = "opencrank C++11";
    
    // Default system prompt for the AI assistant
    static const char* default_system_prompt();
};

// ============================================================================
// Application Class - Singleton
// ============================================================================

class Application {
public:
    // Singleton access
    static Application& instance();
    
    // Prevent copying
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    
    // ==================== Accessors ====================
    
    Config& config() { return config_; }
    const Config& config() const { return config_; }
    
    PluginLoader& loader() { return loader_; }
    PluginRegistry& registry() { return PluginRegistry::instance(); }
    SessionManager& sessions() { return SessionManager::instance(); }
    ThreadPool* thread_pool() { return thread_pool_; }
    
    SkillManager& skills() { return skill_manager_; }
    const std::vector<SkillEntry>& skill_entries() const { return skill_entries_; }
    const std::vector<SkillCommandSpec>& skill_commands() const { return skill_command_specs_; }
    
    Agent& agent() { return agent_; }
    
    // AI process monitor
    AIProcessMonitor& ai_monitor() { return ai_monitor_; }
    
    // Rate limiting
    KeyedRateLimiter& user_limiter() { return user_limiter_; }
    MessageDebouncer& debouncer() { return debouncer_; }
    TypingIndicator& typing() { return typing_; }
    
    // System prompt (can be customized via config)
    const std::string& system_prompt() const { return system_prompt_; }
    void set_system_prompt(const std::string& prompt) { system_prompt_ = prompt; }
    
    // ==================== Lifecycle ====================
    
    // State
    bool is_running() const { return running_.load(); }
    void stop() { running_.store(false); }
    
    // Initialize the application
    bool init(int argc, char* argv[]);
    
    // Run the main loop
    int run();
    
    // Cleanup
    void shutdown();

private:
    // Private constructor for singleton
    Application();
    
    // Initialization helpers
    bool parse_args(int argc, char* argv[]);
    void setup_sandbox();      // Phase 1: create dirs, override paths
    void activate_sandbox();   // Phase 2: activate Landlock (after plugins loaded)
    void setup_logging();
    void setup_skills();
    void setup_agent();
    void setup_plugins();
    void setup_channels();
    void warmup_ai();
    
    // State
    std::atomic<bool> running_;
    
    // Core components
    Config config_;
    PluginLoader loader_;
    ThreadPool* thread_pool_;
    Agent agent_;
    AIProcessMonitor ai_monitor_;
    
    // Rate limiting
    KeyedRateLimiter user_limiter_;
    MessageDebouncer debouncer_;
    TypingIndicator typing_;
    
    // Skills system
    SkillManager skill_manager_;
    std::vector<SkillEntry> skill_entries_;
    std::vector<SkillCommandSpec> skill_command_specs_;
    
    // System prompt
    std::string system_prompt_;
    std::string config_file_;
};

// ============================================================================
// Utility Functions
// ============================================================================

// Print help message
void print_usage(const char* prog);

// Print version
void print_version();

// Split text into chunks for large messages
std::vector<std::string> split_message_chunks(const std::string& text, size_t max_len);

} // namespace opencrank

#endif // opencrank_CORE_APPLICATION_HPP
