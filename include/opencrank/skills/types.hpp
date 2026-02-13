/*
 * opencrank C++ - Skills Types
 * 
 * Data structures for the skills system.
 * Based on opencrank's skill/prompt architecture.
 */
#ifndef opencrank_SKILLS_TYPES_HPP
#define opencrank_SKILLS_TYPES_HPP

#include <string>
#include <vector>
#include <map>

namespace opencrank {

// Installation spec for a skill dependency
struct SkillInstallSpec {
    std::string id;
    std::string kind;       // "brew", "node", "go", "uv", "download"
    std::string label;
    std::vector<std::string> bins;
    std::vector<std::string> os;  // Platform restrictions: "darwin", "linux", "win32"
    std::string formula;    // For brew
    std::string package;    // For node/go/uv
    std::string module;     // For go
    std::string url;        // For download
    std::string archive;    // Archive format
    bool extract;
    int strip_components;
    std::string target_dir;
    
    SkillInstallSpec() : extract(false), strip_components(0) {}
};

// Requirements for a skill to be eligible
struct SkillRequirements {
    std::vector<std::string> bins;      // All of these binaries must exist
    std::vector<std::string> any_bins;  // At least one of these must exist
    std::vector<std::string> env;       // Required environment variables
    std::vector<std::string> config;    // Required config paths
    
    bool empty() const {
        return bins.empty() && any_bins.empty() && env.empty() && config.empty();
    }
};

// opencrank-specific skill metadata (from frontmatter)
struct SkillMetadata {
    bool always;            // Always include this skill
    std::string skill_key;  // Override key for deduplication
    std::string primary_env; // Primary environment variable
    std::string emoji;      // Emoji for display
    std::string homepage;   // Homepage URL
    std::vector<std::string> os; // OS restrictions
    SkillRequirements requirements;  // Platform/bin requirements
    std::vector<SkillInstallSpec> install;
    
    SkillMetadata() : always(false) {}
};

// Invocation policy for a skill
struct SkillInvocationPolicy {
    bool user_invocable;           // Can be invoked by user command
    bool disable_model_invocation; // Exclude from model's skill prompt
    
    SkillInvocationPolicy() : user_invocable(true), disable_model_invocation(false) {}
};

// Dispatch spec for skill commands
struct SkillCommandDispatch {
    std::string kind;     // "tool"
    std::string tool_name;
    std::string arg_mode; // "raw"
    
    bool empty() const { return kind.empty(); }
};

// A skill command specification
struct SkillCommandSpec {
    std::string name;        // Command name (sanitized)
    std::string skill_name;  // Original skill name
    std::string description;
    SkillCommandDispatch dispatch;
};

// Parsed frontmatter from SKILL.md
typedef std::map<std::string, std::string> SkillFrontmatter;

// A loaded skill
struct Skill {
    std::string name;        // Skill identifier
    std::string description; // Short description
    std::string file_path;   // Path to SKILL.md
    std::string base_dir;    // Directory containing the skill
    std::string source;      // Source: "workspace", "managed", "bundled", "extra"
    std::string content;     // Full content of SKILL.md
    
    bool empty() const { return name.empty(); }
};

// A skill entry with all metadata
struct SkillEntry {
    Skill skill;
    SkillFrontmatter frontmatter;
    SkillMetadata metadata;
    SkillInvocationPolicy invocation;
    
    bool empty() const { return skill.empty(); }
};

// Context for checking skill eligibility (for remote/sandboxed environments)
struct SkillEligibilityContext {
    struct Remote {
        std::vector<std::string> platforms;
        std::string note;
        
        bool has_bin(const std::string& bin) const;
        bool has_any_bin(const std::vector<std::string>& bins) const;
    };
    
    Remote remote;
    bool has_remote;
    
    SkillEligibilityContext() : has_remote(false) {}
};

// Snapshot of skills for a session/run
struct SkillSnapshot {
    std::string prompt;      // Combined prompt text
    std::vector<std::pair<std::string, std::string>> skills; // name, primary_env
    int version;
    
    SkillSnapshot() : version(1) {}
    
    bool empty() const { return skills.empty(); }
};

// Configuration for skill loading
struct SkillsConfig {
    std::string workspace_dir;
    std::string managed_skills_dir;  // ~/.config/opencrank/skills
    std::string bundled_skills_dir;  // Built-in skills
    std::vector<std::string> extra_dirs;
    std::vector<std::string> skill_filter; // If set, only include these skills
    
    // Installation preferences
    bool prefer_brew;
    std::string node_manager; // "npm", "pnpm", "yarn", "bun"
    
    SkillsConfig() : prefer_brew(true), node_manager("npm") {}
};

} // namespace opencrank

#endif // opencrank_SKILLS_TYPES_HPP
