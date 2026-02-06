/*
 * OpenCrank C++11 - Skills Loader
 * 
 * Loads skills from SKILL.md files in directories.
 * Parses frontmatter and builds SkillEntry objects.
 */
#ifndef OPENCRANK_SKILLS_LOADER_HPP
#define OPENCRANK_SKILLS_LOADER_HPP

#include "types.hpp"
#include <string>
#include <vector>

namespace opencrank {

class SkillLoader {
public:
    SkillLoader();
    ~SkillLoader();
    
    // Load skills from a directory (non-recursive scan for subdirs with SKILL.md)
    std::vector<Skill> load_from_dir(const std::string& dir, const std::string& source = "");
    
    // Load a single skill from a directory containing SKILL.md
    Skill load_skill(const std::string& skill_dir, const std::string& source = "");
    
    // Parse frontmatter from SKILL.md content
    SkillFrontmatter parse_frontmatter(const std::string& content);
    
    // Resolve OpenCrank metadata from frontmatter
    SkillMetadata resolve_metadata(const SkillFrontmatter& frontmatter);
    
    // Resolve invocation policy from frontmatter
    SkillInvocationPolicy resolve_invocation_policy(const SkillFrontmatter& frontmatter);
    
    // Build a SkillEntry from a Skill
    SkillEntry build_entry(const Skill& skill);
    
    // Get the content after frontmatter (the actual skill instructions)
    std::string get_content_body(const std::string& content);
    
    // Format skill for prompt inclusion
    std::string format_skill_for_prompt(const Skill& skill);
    
    // Last error message
    const std::string& last_error() const { return last_error_; }
    
private:
    std::string last_error_;
    
    // Parse a JSON5-ish metadata object from frontmatter
    bool parse_metadata_json(const std::string& json_str, SkillMetadata& metadata);
    
    // Parse install spec from JSON-like structure
    bool parse_install_spec(const std::string& json_str, SkillInstallSpec& spec);
    
    // Parse requirements from JSON-like structure
    bool parse_requirements(const std::string& json_str, SkillRequirements& reqs);
    
    // Helper: read file contents
    std::string read_file(const std::string& path);
    
    // Helper: check if directory exists
    bool dir_exists(const std::string& path);
    
    // Helper: check if file exists
    bool file_exists(const std::string& path);
    
    // Helper: list subdirectories
    std::vector<std::string> list_subdirs(const std::string& dir);
    
    // Helper: trim whitespace
    std::string trim(const std::string& str);
    
    // Helper: to lowercase
    std::string to_lower(const std::string& str);
    
    // Helper: split string
    std::vector<std::string> split(const std::string& str, char delim);
    
    // Helper: parse boolean from string
    bool parse_bool(const std::string& str, bool default_val = false);
    
    void set_error(const std::string& err);
};

} // namespace opencrank

#endif // OPENCRANK_SKILLS_LOADER_HPP
