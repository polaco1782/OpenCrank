/*
 * OpenCrank C++11 - Skills Manager Implementation
 * 
 * Manages skills loading, filtering, and prompt generation.
 */
#include <opencrank/skills/manager.hpp>
#include <opencrank/core/logger.hpp>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define PATH_SEP '\\'
#else
#include <unistd.h>
#define PATH_SEP '/'
#endif

namespace opencrank {

static const int SKILL_COMMAND_MAX_LENGTH = 32;
static const int SKILL_COMMAND_DESCRIPTION_MAX_LENGTH = 100;

SkillManager::SkillManager() {}

SkillManager::SkillManager(const SkillsConfig& config) : config_(config) {}

SkillManager::~SkillManager() {}

void SkillManager::set_config(const SkillsConfig& config) {
    config_ = config;
}

std::vector<SkillEntry> SkillManager::load_workspace_skill_entries() {
    LOG_INFO("[SkillManager] Loading workspace skill entries...");
    LOG_DEBUG("[SkillManager] Loading workspace skill entries");
    std::map<std::string, SkillEntry> merged;
    
    // Precedence: extra < bundled < managed < workspace
    
    // 1. Load from extra directories
    LOG_DEBUG("[SkillManager] Loading from %zu extra directories", config_.extra_dirs.size());
    for (size_t i = 0; i < config_.extra_dirs.size(); ++i) {
        std::vector<SkillEntry> entries = load_entries_from_dir(
            config_.extra_dirs[i], "opencrank-extra");
        for (size_t j = 0; j < entries.size(); ++j) {
            merged[entries[j].skill.name] = entries[j];
        }
    }
    
    // 2. Load bundled skills
    if (!config_.bundled_skills_dir.empty()) {
        LOG_DEBUG("[SkillManager] Loading bundled skills from: %s", config_.bundled_skills_dir.c_str());
        std::vector<SkillEntry> entries = load_entries_from_dir(
            config_.bundled_skills_dir, "opencrank-bundled");
        LOG_DEBUG("[SkillManager] Loaded %zu bundled skills", entries.size());
        for (size_t j = 0; j < entries.size(); ++j) {
            merged[entries[j].skill.name] = entries[j];
        }
    }
    
    // 3. Load managed skills
    if (!config_.managed_skills_dir.empty()) {
        LOG_DEBUG("[SkillManager] Loading managed skills from: %s", config_.managed_skills_dir.c_str());
        std::vector<SkillEntry> entries = load_entries_from_dir(
            config_.managed_skills_dir, "opencrank-managed");
        LOG_DEBUG("[SkillManager] Loaded %zu managed skills", entries.size());
        for (size_t j = 0; j < entries.size(); ++j) {
            merged[entries[j].skill.name] = entries[j];
        }
    }
    
    // 4. Load workspace skills (highest priority)
    if (!config_.workspace_dir.empty()) {
        std::string workspace_skills_dir = config_.workspace_dir + "/skills";
        LOG_DEBUG("[SkillManager] Loading workspace skills from: %s", workspace_skills_dir.c_str());
        std::vector<SkillEntry> entries = load_entries_from_dir(
            workspace_skills_dir, "opencrank-workspace");
        LOG_DEBUG("[SkillManager] Loaded %zu workspace skills", entries.size());
        for (size_t j = 0; j < entries.size(); ++j) {
            merged[entries[j].skill.name] = entries[j];
        }
    }
    
    // Convert map to vector
    std::vector<SkillEntry> result;
    for (std::map<std::string, SkillEntry>::iterator it = merged.begin();
         it != merged.end(); ++it) {
        result.push_back(it->second);
    }
    
    LOG_DEBUG("[SkillManager] Total skills after merge: %zu", result.size());
    return result;
}

std::vector<SkillEntry> SkillManager::load_entries_from_dir(
    const std::string& dir,
    const std::string& source) {
    
    LOG_INFO("[SkillManager] load_entries_from_dir called: dir='%s', source='%s'", dir.c_str(), source.c_str());
    std::vector<SkillEntry> entries;
    std::vector<Skill> skills = loader_.load_from_dir(dir, source);
    LOG_INFO("[SkillManager] load_from_dir returned %zu skills", skills.size());
    
    for (size_t i = 0; i < skills.size(); ++i) {
        SkillEntry entry = loader_.build_entry(skills[i]);
        if (!entry.empty()) {
            entries.push_back(entry);
        }
    }
    
    return entries;
}

std::vector<SkillEntry> SkillManager::filter_skill_entries(
    const std::vector<SkillEntry>& entries,
    const SkillEligibilityContext* eligibility) {
    
    LOG_DEBUG("[SkillManager] Filtering %zu skill entries", entries.size());
    std::vector<SkillEntry> filtered;
    
    for (size_t i = 0; i < entries.size(); ++i) {
        if (should_include_skill(entries[i], eligibility)) {
            LOG_DEBUG("[SkillManager] Including skill: %s", entries[i].skill.name.c_str());
            filtered.push_back(entries[i]);
        } else {
            LOG_DEBUG("[SkillManager] Filtering out skill: %s", entries[i].skill.name.c_str());
        }
    }
    
    // Apply skill filter if set
    if (!config_.skill_filter.empty()) {
        LOG_DEBUG("[SkillManager] Applying skill filter with %zu allowed skills", config_.skill_filter.size());
        std::set<std::string> filter_set(
            config_.skill_filter.begin(),
            config_.skill_filter.end());
        
        std::vector<SkillEntry> final_filtered;
        for (size_t i = 0; i < filtered.size(); ++i) {
            if (filter_set.count(filtered[i].skill.name) > 0) {
                final_filtered.push_back(filtered[i]);
            }
        }
        LOG_DEBUG("[SkillManager] After filter: %zu skills", final_filtered.size());
        return final_filtered;
    }
    
    LOG_DEBUG("[SkillManager] Filter complete: %zu eligible skills", filtered.size());
    return filtered;
}

bool SkillManager::should_include_skill(
    const SkillEntry& entry,
    const SkillEligibilityContext* eligibility) {
    
    const SkillMetadata& meta = entry.metadata;
    
    // Check OS restrictions
    if (!meta.os.empty()) {
        std::string current_os = get_platform();
        bool os_match = false;
        for (size_t i = 0; i < meta.os.size(); ++i) {
            if (meta.os[i] == current_os) {
                os_match = true;
                break;
            }
        }
        if (!os_match) {
            LOG_DEBUG("[SkillManager]   ✗ %s: OS mismatch (requires %s, have %s)", 
                     entry.skill.name.c_str(), meta.os[0].c_str(), current_os.c_str());
            return false;
        }
    }
    
    // Check requirements
    const SkillRequirements& reqs = meta.requirements;
    
    // Check required binaries
    if (!reqs.bins.empty()) {
        for (size_t i = 0; i < reqs.bins.size(); ++i) {
            bool found = false;
            if (eligibility && eligibility->has_remote) {
                found = eligibility->remote.has_bin(reqs.bins[i]);
            } else {
                found = has_binary(reqs.bins[i]);
            }
            if (!found) {
                LOG_DEBUG("[SkillManager]   ✗ %s: missing required binary '%s'", 
                         entry.skill.name.c_str(), reqs.bins[i].c_str());
                return false;
            }
        }
    }
    
    // Check anyBins (at least one must exist)
    if (!reqs.any_bins.empty()) {
        bool any_found = false;
        for (size_t i = 0; i < reqs.any_bins.size(); ++i) {
            bool found = false;
            if (eligibility && eligibility->has_remote) {
                found = eligibility->remote.has_bin(reqs.any_bins[i]);
            } else {
                found = has_binary(reqs.any_bins[i]);
            }
            if (found) {
                any_found = true;
                break;
            }
        }
        if (!any_found) {
            std::string bins_list;
            for (size_t i = 0; i < reqs.any_bins.size(); ++i) {
                if (i > 0) bins_list += ", ";
                bins_list += reqs.any_bins[i];
            }
            LOG_DEBUG("[SkillManager]   ✗ %s: none of required binaries found: %s", 
                     entry.skill.name.c_str(), bins_list.c_str());
            return false;
        }
    }
    
    // Check environment variables
    if (!reqs.env.empty()) {
        for (size_t i = 0; i < reqs.env.size(); ++i) {
            if (!has_env(reqs.env[i])) {
                LOG_DEBUG("[SkillManager]   ✗ %s: missing required env var '%s'", 
                         entry.skill.name.c_str(), reqs.env[i].c_str());
                return false;
            }
        }
    }
    
    return true;
}

SkillSnapshot SkillManager::build_workspace_skill_snapshot(
    const std::vector<SkillEntry>* entries,
    const SkillEligibilityContext* eligibility) {
    
    SkillSnapshot snapshot;
    snapshot.version = 1;
    
    // Load entries if not provided
    std::vector<SkillEntry> loaded_entries;
    if (entries) {
        loaded_entries = *entries;
    } else {
        loaded_entries = load_workspace_skill_entries();
    }
    
    // Filter entries
    std::vector<SkillEntry> eligible = filter_skill_entries(loaded_entries, eligibility);
    
    // Filter out entries with disabled model invocation
    std::vector<SkillEntry> prompt_entries;
    for (size_t i = 0; i < eligible.size(); ++i) {
        if (!eligible[i].invocation.disable_model_invocation) {
            prompt_entries.push_back(eligible[i]);
        } else {
            LOG_DEBUG("[SkillManager] Excluding %s from prompt (model invocation disabled)", 
                     eligible[i].skill.name.c_str());
        }
    }
    
    // Build skills list
    for (size_t i = 0; i < eligible.size(); ++i) {
        snapshot.skills.push_back(std::make_pair(
            eligible[i].skill.name,
            eligible[i].metadata.primary_env));
    }
    
    // Build prompt
    std::vector<Skill> skills_for_prompt;
    for (size_t i = 0; i < prompt_entries.size(); ++i) {
        skills_for_prompt.push_back(prompt_entries[i].skill);
    }
    snapshot.prompt = format_skills_for_prompt(skills_for_prompt);
    
    return snapshot;
}

std::string SkillManager::build_workspace_skills_prompt(
    const std::vector<SkillEntry>* entries,
    const SkillEligibilityContext* eligibility) {
    
    // Load entries if not provided
    std::vector<SkillEntry> loaded_entries;
    if (entries) {
        loaded_entries = *entries;
    } else {
        loaded_entries = load_workspace_skill_entries();
    }
    
    // Filter entries
    std::vector<SkillEntry> eligible = filter_skill_entries(loaded_entries, eligibility);
    
    // Filter out entries with disabled model invocation
    std::vector<Skill> prompt_skills;
    for (size_t i = 0; i < eligible.size(); ++i) {
        if (!eligible[i].invocation.disable_model_invocation) {
            prompt_skills.push_back(eligible[i].skill);
        }
    }
    
    return format_skills_for_prompt(prompt_skills);
}

std::string SkillManager::format_skills_for_prompt(const std::vector<Skill>& skills) {
    if (skills.empty()) {
        LOG_DEBUG("[SkillManager] No skills to format for prompt");
        return "";
    }
    
    LOG_DEBUG("[SkillManager] Formatting %zu skills for AI prompt", skills.size());
    
    std::ostringstream oss;
    oss << "<available_skills>\n";
    
    for (size_t i = 0; i < skills.size(); ++i) {
        const Skill& skill = skills[i];
        LOG_DEBUG("[SkillManager]   Adding skill to prompt: %s (location: %s)", 
                 skill.name.c_str(), skill.file_path.c_str());
        oss << "<skill name=\"" << skill.name << "\" location=\"" << skill.file_path << "\">\n";
        oss << "  <description>" << skill.description << "</description>\n";
        oss << "</skill>\n";
    }
    
    oss << "</available_skills>";
    return oss.str();
}

std::string SkillManager::build_skill_instructions(bool /* include_skills_xml */) {
    LOG_DEBUG("[SkillManager] Building skill instructions for AI prompt");
    
    std::ostringstream oss;
    
    oss << "## Skills System (MANDATORY)\n\n";
    oss << "You have access to skills that extend your capabilities. Skills are defined in SKILL.md files.\n\n";
    
    oss << "### Skill Discovery Process:\n";
    oss << "1. SCAN: Review the <available_skills> section below to see what skills are available\n";
    oss << "2. MATCH: Determine if the user's request matches a skill's description\n";
    oss << "3. READ: If a skill applies, use the `read` tool to read the full SKILL.md file:\n";
    oss << "   {\"tool\": \"read\", \"arguments\": {\"path\": \"./skills/weather/SKILL.md\"}}\n";
    oss << "4. EXECUTE: Follow the instructions in the SKILL.md to complete the task\n\n";
    
    oss << "### Critical Rules:\n";
    oss << "- ALWAYS read the SKILL.md file BEFORE attempting to execute the skill\n";
    oss << "- The SKILL.md contains the EXACT commands and tool parameters you must use\n";
    oss << "- Skills define HOW to accomplish tasks - follow them precisely\n";
    oss << "- Use the `shell` tool to execute commands shown in the SKILL.md\n";
    oss << "- If multiple skills could apply, choose the most specific one\n";
    oss << "- If no skill clearly applies, respond without using a skill\n\n";
    
    oss << "### Example Workflow:\n";
    oss << "User: \"Run this Python code: print('hello')\"\n";
    oss << "1. Scan available_skills -> find skill with python in description\n";
    oss << "2. Read the SKILL.md file using: {\"tool\": \"read\", \"arguments\": {\"path\": \"path/to/SKILL.md\"}}\n";
    oss << "3. Follow the instructions in SKILL.md (e.g., use shell tool with python3 command)\n";
    oss << "4. Return the result to the user\n\n";
    
    LOG_DEBUG("[SkillManager] Built skill instructions (%zu bytes)", oss.str().size());
    return oss.str();
}

std::string SkillManager::build_skills_section(const std::vector<SkillEntry>* entries) {
    LOG_DEBUG("[SkillManager] Building full skills section for system prompt");
    
    // Get the skills XML
    std::string skills_xml = build_workspace_skills_prompt(entries, NULL);
    
    if (skills_xml.empty()) {
        LOG_DEBUG("[SkillManager] No skills available, returning empty section");
        return "";  // No skills, no section
    }
    
    // Build full section: instructions + skills XML
    std::ostringstream oss;
    oss << build_skill_instructions(false);
    oss << "\n";
    oss << skills_xml;
    
    std::string result = oss.str();
    LOG_INFO("[SkillManager] Built skills section with %zu skills", 
             entries ? entries->size() : 0);
    LOG_DEBUG("[SkillManager] Skills section:\n%s", result.c_str());
    
    return result;
}

std::vector<SkillCommandSpec> SkillManager::build_workspace_skill_command_specs(
    const std::vector<SkillEntry>* entries,
    const SkillEligibilityContext* eligibility,
    const std::set<std::string>* reserved_names) {
    
    // Load entries if not provided
    std::vector<SkillEntry> loaded_entries;
    if (entries) {
        loaded_entries = *entries;
    } else {
        loaded_entries = load_workspace_skill_entries();
    }
    
    // Filter entries
    std::vector<SkillEntry> eligible = filter_skill_entries(loaded_entries, eligibility);
    
    // Filter to user-invocable skills
    std::vector<SkillEntry> user_invocable;
    for (size_t i = 0; i < eligible.size(); ++i) {
        if (eligible[i].invocation.user_invocable) {
            user_invocable.push_back(eligible[i]);
        }
    }
    
    // Track used names
    std::set<std::string> used;
    if (reserved_names) {
        for (std::set<std::string>::const_iterator it = reserved_names->begin();
             it != reserved_names->end(); ++it) {
            std::string lower = *it;
            for (size_t i = 0; i < lower.size(); ++i) {
                lower[i] = tolower(lower[i]);
            }
            used.insert(lower);
        }
    }
    
    std::vector<SkillCommandSpec> specs;
    
    for (size_t i = 0; i < user_invocable.size(); ++i) {
        const SkillEntry& entry = user_invocable[i];
        const std::string& raw_name = entry.skill.name;
        
        std::string base = sanitize_skill_command_name(raw_name);
        std::string unique = resolve_unique_skill_command_name(base, used);
        
        std::string lower_unique = unique;
        for (size_t j = 0; j < lower_unique.size(); ++j) {
            lower_unique[j] = tolower(lower_unique[j]);
        }
        used.insert(lower_unique);
        
        // Build description
        std::string raw_desc = entry.skill.description;
        if (raw_desc.empty()) {
            raw_desc = raw_name;
        }
        std::string description = raw_desc;
        if (description.size() > SKILL_COMMAND_DESCRIPTION_MAX_LENGTH) {
            description = description.substr(0, SKILL_COMMAND_DESCRIPTION_MAX_LENGTH - 1) + "...";
        }
        
        // Build dispatch if specified
        SkillCommandDispatch dispatch;
        if (entry.frontmatter.count("command-dispatch") ||
            entry.frontmatter.count("command_dispatch")) {
            std::string kind = entry.frontmatter.count("command-dispatch")
                ? entry.frontmatter.at("command-dispatch")
                : entry.frontmatter.at("command_dispatch");
            
            // Lowercase and trim
            for (size_t j = 0; j < kind.size(); ++j) {
                kind[j] = tolower(kind[j]);
            }
            size_t start = kind.find_first_not_of(" \t\n\r");
            size_t end = kind.find_last_not_of(" \t\n\r");
            if (start != std::string::npos && end != std::string::npos) {
                kind = kind.substr(start, end - start + 1);
            }
            
            if (kind == "tool") {
                dispatch.kind = "tool";
                
                if (entry.frontmatter.count("command-tool")) {
                    dispatch.tool_name = entry.frontmatter.at("command-tool");
                } else if (entry.frontmatter.count("command_tool")) {
                    dispatch.tool_name = entry.frontmatter.at("command_tool");
                }
                
                dispatch.arg_mode = "raw";
            }
        }
        
        SkillCommandSpec spec;
        spec.name = unique;
        spec.skill_name = raw_name;
        spec.description = description;
        spec.dispatch = dispatch;
        
        specs.push_back(spec);
    }
    
    return specs;
}

std::string SkillManager::resolve_skills_prompt_for_run(
    const SkillSnapshot* snapshot,
    const std::vector<SkillEntry>* entries) {
    
    // Prefer snapshot prompt if available
    if (snapshot) {
        std::string prompt = snapshot->prompt;
        // Trim
        size_t start = prompt.find_first_not_of(" \t\n\r");
        size_t end = prompt.find_last_not_of(" \t\n\r");
        if (start != std::string::npos && end != std::string::npos) {
            prompt = prompt.substr(start, end - start + 1);
        }
        if (!prompt.empty()) {
            return prompt;
        }
    }
    
    // Fall back to building from entries
    if (entries && !entries->empty()) {
        return build_workspace_skills_prompt(entries, NULL);
    }
    
    return "";
}

std::string SkillManager::sanitize_skill_command_name(const std::string& raw) {
    std::string normalized;
    
    // Lowercase and replace non-alphanumeric with underscore
    for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (isalnum(c)) {
            normalized += tolower(c);
        } else {
            normalized += '_';
        }
    }
    
    // Collapse multiple underscores
    std::string collapsed;
    bool last_was_underscore = false;
    for (size_t i = 0; i < normalized.size(); ++i) {
        if (normalized[i] == '_') {
            if (!last_was_underscore) {
                collapsed += '_';
                last_was_underscore = true;
            }
        } else {
            collapsed += normalized[i];
            last_was_underscore = false;
        }
    }
    
    // Trim leading/trailing underscores
    size_t start = collapsed.find_first_not_of('_');
    size_t end = collapsed.find_last_not_of('_');
    if (start == std::string::npos) {
        return "skill";  // Fallback
    }
    std::string trimmed = collapsed.substr(start, end - start + 1);
    
    // Truncate to max length
    if (trimmed.size() > SKILL_COMMAND_MAX_LENGTH) {
        trimmed = trimmed.substr(0, SKILL_COMMAND_MAX_LENGTH);
    }
    
    return trimmed.empty() ? "skill" : trimmed;
}

std::string SkillManager::resolve_unique_skill_command_name(
    const std::string& base,
    std::set<std::string>& used) {
    
    std::string normalized_base = base;
    for (size_t i = 0; i < normalized_base.size(); ++i) {
        normalized_base[i] = tolower(normalized_base[i]);
    }
    
    if (used.count(normalized_base) == 0) {
        return base;
    }
    
    // Try adding suffix
    for (int index = 2; index < 1000; ++index) {
        std::ostringstream oss;
        oss << "_" << index;
        std::string suffix = oss.str();
        
        int max_base_len = SKILL_COMMAND_MAX_LENGTH - (int)suffix.size();
        if (max_base_len < 1) max_base_len = 1;
        
        std::string trimmed_base = base.substr(0, max_base_len);
        std::string candidate = trimmed_base + suffix;
        
        std::string candidate_key = candidate;
        for (size_t i = 0; i < candidate_key.size(); ++i) {
            candidate_key[i] = tolower(candidate_key[i]);
        }
        
        if (used.count(candidate_key) == 0) {
            return candidate;
        }
    }
    
    // Fallback
    std::string fallback = base.substr(0, SKILL_COMMAND_MAX_LENGTH - 2) + "_x";
    return fallback;
}

bool SkillManager::has_binary(const std::string& bin) {
    // Check if binary exists in PATH
    const char* path_env = getenv("PATH");
    if (!path_env) {
        return false;
    }
    
    std::string path_str = path_env;
    
#ifdef _WIN32
    char delim = ';';
    std::string exe_suffix = ".exe";
#else
    char delim = ':';
    std::string exe_suffix = "";
#endif
    
    std::istringstream iss(path_str);
    std::string dir;
    while (std::getline(iss, dir, delim)) {
        std::string full_path = dir + PATH_SEP + bin + exe_suffix;
        struct stat st;
        if (stat(full_path.c_str(), &st) == 0 && (st.st_mode & S_IXUSR)) {
            return true;
        }
    }
    
    return false;
}

bool SkillManager::has_env(const std::string& var) {
    const char* val = getenv(var.c_str());
    return val != NULL && val[0] != '\0';
}

std::string SkillManager::get_platform() {
#if defined(__APPLE__)
    return "darwin";
#elif defined(_WIN32)
    return "win32";
#else
    return "linux";
#endif
}

void SkillManager::set_error(const std::string& err) {
    last_error_ = err;
}

std::pair<const SkillCommandSpec*, std::string> SkillManager::resolve_skill_command_invocation(
    const std::string& user_input,
    const std::vector<SkillCommandSpec>& commands) {
    
    std::pair<const SkillCommandSpec*, std::string> result(NULL, "");
    
    if (commands.empty()) {
        return result;
    }

    LOG_DEBUG("[SkillManager] Resolving skill command invocation for input: %s", user_input.c_str());
    
    // Trim input
    std::string input = user_input;
    size_t start = input.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return result;
    }
    input = input.substr(start);
    
    // Must start with /
    if (input.empty() || input[0] != '/') {
        return result;
    }
    
    // Remove leading /
    input = input.substr(1);
    
    // Check for /skill prefix (e.g., "/skill coding-agent args")
    if (input.size() >= 5) {
        std::string prefix = input.substr(0, 5);
        for (size_t i = 0; i < prefix.size(); ++i) {
            prefix[i] = tolower(prefix[i]);
        }
        if (prefix == "skill" && (input.size() == 5 || input[5] == ' ' || input[5] == '\t')) {
            input = input.substr(5);
            // Trim leading whitespace
            start = input.find_first_not_of(" \t");
            if (start != std::string::npos) {
                input = input.substr(start);
            } else {
                input.clear();
            }
        }
    }
    
    if (input.empty()) {
        return result;
    }
    
    // Split into command name and arguments
    std::string cmd_name;
    std::string args;
    size_t space_pos = input.find_first_of(" \t");
    if (space_pos != std::string::npos) {
        cmd_name = input.substr(0, space_pos);
        args = input.substr(space_pos + 1);
        // Trim args
        start = args.find_first_not_of(" \t");
        if (start != std::string::npos) {
            args = args.substr(start);
            size_t end = args.find_last_not_of(" \t\n\r");
            if (end != std::string::npos) {
                args = args.substr(0, end + 1);
            }
        } else {
            args.clear();
        }
    } else {
        cmd_name = input;
    }
    
    // Normalize command name for comparison
    std::string normalized_cmd = cmd_name;
    for (size_t i = 0; i < normalized_cmd.size(); ++i) {
        normalized_cmd[i] = tolower(normalized_cmd[i]);
    }
    
    // Find matching command
    for (size_t i = 0; i < commands.size(); ++i) {
        std::string cmd_lower = commands[i].name;
        for (size_t j = 0; j < cmd_lower.size(); ++j) {
            cmd_lower[j] = tolower(cmd_lower[j]);
        }
        
        if (cmd_lower == normalized_cmd) {
            result.first = &commands[i];
            result.second = args;
            return result;
        }
    }
    
    // Also check skill_name if different from name
    for (size_t i = 0; i < commands.size(); ++i) {
        std::string skill_lower = commands[i].skill_name;
        for (size_t j = 0; j < skill_lower.size(); ++j) {
            skill_lower[j] = tolower(skill_lower[j]);
        }
        
        if (skill_lower == normalized_cmd) {
            result.first = &commands[i];
            result.second = args;
            return result;
        }
    }
    
    return result;
}

const SkillEntry* SkillManager::find_skill_by_name(
    const std::string& name,
    const std::vector<SkillEntry>& entries) {
    
    std::string normalized_name = name;
    for (size_t i = 0; i < normalized_name.size(); ++i) {
        normalized_name[i] = tolower(normalized_name[i]);
    }
    
    for (size_t i = 0; i < entries.size(); ++i) {
        std::string entry_name = entries[i].skill.name;
        for (size_t j = 0; j < entry_name.size(); ++j) {
            entry_name[j] = tolower(entry_name[j]);
        }
        
        if (entry_name == normalized_name) {
            return &entries[i];
        }
    }
    
    return NULL;
}

std::string SkillManager::list_skills_for_display(
    const std::vector<SkillEntry>& entries,
    bool show_eligibility) {
    
    std::ostringstream oss;
    
    if (entries.empty()) {
        oss << "No skills loaded.\n";
        return oss.str();
    }
    
    oss << "Loaded Skills:\n";
    oss << "──────────────\n";
    
    for (size_t i = 0; i < entries.size(); ++i) {
        const SkillEntry& entry = entries[i];
        
        // Status indicator
        std::string status;
        if (show_eligibility) {
            if (should_include_skill(entry, NULL)) {
                status = "(good)";
            } else {
                status = "(bad)";
            }
        }
        
        // Emoji
        std::string emoji = entry.metadata.emoji;
        if (emoji.empty()) {
            emoji = "⚡";
        }
        
        // Build line
        oss << "  " << status << " " << emoji << " " << entry.skill.name;
        
        // Source
        oss << " (" << entry.skill.source << ")";
        
        // Invocation info
        if (entry.invocation.user_invocable) {
            oss << " [/" << sanitize_skill_command_name(entry.skill.name) << "]";
        }
        
        oss << "\n";
        
        // Description
        if (!entry.skill.description.empty()) {
            oss << "      " << entry.skill.description << "\n";
        }
    }
    
    return oss.str();
}

// Helper functions

std::string resolve_bundled_skills_dir() {
    // Check for OPENCRANK_SKILLS_DIR environment variable
    const char* env_dir = getenv("OPENCRANK_SKILLS_DIR");
    if (env_dir && env_dir[0] != '\0') {
        return env_dir;
    }
    
    // Default to relative path from executable
    // In production, this would be determined by installation location
    return "";
}

std::string resolve_managed_skills_dir() {
    const char* home = getenv("HOME");
    if (!home) {
        home = getenv("USERPROFILE");
    }
    if (!home) {
        return "";
    }
    
    return std::string(home) + "/.config/opencrank/skills";
}

} // namespace opencrank
