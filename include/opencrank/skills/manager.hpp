/*
 * opencrank C++11 - Skills Manager
 * 
 * Manages skills loading, filtering, and prompt generation.
 * Builds skill snapshots and prompts for AI consumption.
 */
#ifndef opencrank_SKILLS_MANAGER_HPP
#define opencrank_SKILLS_MANAGER_HPP

#include "types.hpp"
#include "loader.hpp"
#include <string>
#include <vector>
#include <set>

namespace opencrank {

class SkillManager {
public:
    SkillManager();
    explicit SkillManager(const SkillsConfig& config);
    ~SkillManager();
    
    // Set configuration
    void set_config(const SkillsConfig& config);
    const SkillsConfig& config() const { return config_; }
    
    // Load all skills from configured directories
    // Precedence: extra < bundled < managed < workspace
    std::vector<SkillEntry> load_workspace_skill_entries();
    
    // Filter entries based on config and eligibility
    std::vector<SkillEntry> filter_skill_entries(
        const std::vector<SkillEntry>& entries,
        const SkillEligibilityContext* eligibility = NULL);
    
    // Build a skill snapshot for a run
    SkillSnapshot build_workspace_skill_snapshot(
        const std::vector<SkillEntry>* entries = NULL,
        const SkillEligibilityContext* eligibility = NULL);
    
    // Build skills prompt for system prompt injection
    std::string build_workspace_skills_prompt(
        const std::vector<SkillEntry>* entries = NULL,
        const SkillEligibilityContext* eligibility = NULL);
    
    // Build skill command specs for chat commands
    std::vector<SkillCommandSpec> build_workspace_skill_command_specs(
        const std::vector<SkillEntry>* entries = NULL,
        const SkillEligibilityContext* eligibility = NULL,
        const std::set<std::string>* reserved_names = NULL);
    
    // Resolve skills prompt for a run (from snapshot or entries)
    std::string resolve_skills_prompt_for_run(
        const SkillSnapshot* snapshot = NULL,
        const std::vector<SkillEntry>* entries = NULL);
    
    // Build skill instructions for system prompt
    // This tells the AI how to use skills: scan, pick one, read SKILL.md
    std::string build_skill_instructions(bool include_skills_xml = true);
    
    // Build full skills section for system prompt (instructions + skills XML)
    std::string build_skills_section(const std::vector<SkillEntry>* entries = NULL);
    
    // Check if a skill should be included based on requirements
    bool should_include_skill(
        const SkillEntry& entry,
        const SkillEligibilityContext* eligibility = NULL);
    
    // Resolve skill command invocation from user input
    // Parses formats: "/skillname args" or "/skill skillname args"
    // Returns NULL pointer in pair if no matching command found
    std::pair<const SkillCommandSpec*, std::string> resolve_skill_command_invocation(
        const std::string& user_input,
        const std::vector<SkillCommandSpec>& commands);
    
    // Get a skill entry by name
    const SkillEntry* find_skill_by_name(
        const std::string& name,
        const std::vector<SkillEntry>& entries);
    
    // List skills for display (user-friendly format)
    std::string list_skills_for_display(
        const std::vector<SkillEntry>& entries,
        bool show_eligibility = true);
    
    // Check if binary exists
    static bool has_binary(const std::string& bin);
    
    // Check if environment variable is set
    static bool has_env(const std::string& var);
    
    // Get current platform ("darwin", "linux", "win32")
    static std::string get_platform();
    
    // Last error
    const std::string& last_error() const { return last_error_; }
    
private:
    SkillsConfig config_;
    SkillLoader loader_;
    std::string last_error_;
    
    // Load skills from a specific directory
    std::vector<SkillEntry> load_entries_from_dir(
        const std::string& dir,
        const std::string& source);
    
    // Format skills for prompt
    std::string format_skills_for_prompt(const std::vector<Skill>& skills);
    
    // Sanitize command name for use as chat command
    std::string sanitize_skill_command_name(const std::string& raw);
    
    // Get unique command name avoiding collisions
    std::string resolve_unique_skill_command_name(
        const std::string& base,
        std::set<std::string>& used);
    
    void set_error(const std::string& err);
};

} // namespace opencrank

#endif // opencrank_SKILLS_MANAGER_HPP
