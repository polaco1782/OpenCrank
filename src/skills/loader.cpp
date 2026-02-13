/*
 * OpenCrank C++ - Skills Loader Implementation
 * 
 * Loads skills from SKILL.md files, parses frontmatter.
 */
#include <opencrank/skills/loader.hpp>
#include <opencrank/core/logger.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>

namespace opencrank {

SkillLoader::SkillLoader() {}

SkillLoader::~SkillLoader() {}

std::vector<Skill> SkillLoader::load_from_dir(const std::string& dir, const std::string& source) {
    std::vector<Skill> skills;
    
    LOG_INFO(" load_from_dir called: dir='%s', source='%s'", dir.c_str(), source.c_str());
    LOG_DEBUG("Loading skills from directory: %s (source: %s)", dir.c_str(), source.c_str());
    
    if (!dir_exists(dir)) {
        LOG_INFO(" Directory does NOT exist: %s", dir.c_str());
        LOG_DEBUG("Directory does not exist: %s", dir.c_str());
        return skills;
    }
    LOG_INFO(" Directory exists, proceeding with load");
    
    // List subdirectories and look for SKILL.md in each
    std::vector<std::string> subdirs = list_subdirs(dir);
    LOG_DEBUG("Found %zu subdirectories in %s", subdirs.size(), dir.c_str());
    
    for (size_t i = 0; i < subdirs.size(); ++i) {
        const std::string& subdir = subdirs[i];
        std::string skill_dir = dir + "/" + subdir;
        
        LOG_DEBUG("Attempting to load skill from: %s", skill_dir.c_str());
        Skill skill = load_skill(skill_dir, source);
        if (!skill.empty()) {
            LOG_DEBUG("Successfully loaded skill: %s", skill.name.c_str());
            skills.push_back(skill);
        } else {
            LOG_DEBUG("No valid skill found in: %s", skill_dir.c_str());
        }
    }
    
    LOG_INFO(" Loaded %zu skills from %s", skills.size(), dir.c_str());
    return skills;
}

Skill SkillLoader::load_skill(const std::string& skill_dir, const std::string& source) {
    Skill skill;
    
    std::string skill_file = skill_dir + "/SKILL.md";
    LOG_DEBUG("Looking for skill file: %s", skill_file.c_str());
    
    if (!file_exists(skill_file)) {
        LOG_DEBUG("SKILL.md not found in: %s", skill_dir.c_str());
        return skill;
    }
    
    std::string content = read_file(skill_file);
    if (content.empty()) {
        LOG_WARN(" SKILL.md is empty or unreadable: %s", skill_file.c_str());
        return skill;
    }
    
    LOG_DEBUG("Read %zu bytes from %s", content.size(), skill_file.c_str());
    
    // Parse frontmatter to get name and description
    SkillFrontmatter fm = parse_frontmatter(content);
    LOG_DEBUG("Parsed frontmatter with %zu keys", fm.size());
    
    // Get skill name from frontmatter or directory name
    std::string dir_name;
    size_t last_slash = skill_dir.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        dir_name = skill_dir.substr(last_slash + 1);
    } else {
        dir_name = skill_dir;
    }
    
    skill.name = fm.count("name") ? fm["name"] : dir_name;
    skill.description = fm.count("description") ? fm["description"] : "";
    skill.file_path = skill_file;
    skill.base_dir = skill_dir;
    skill.source = source;
    skill.content = content;
    
    LOG_DEBUG("Loaded skill: name='%s', description='%s', path='%s'",
              skill.name.c_str(), skill.description.c_str(), skill.file_path.c_str());
    
    return skill;
}

SkillFrontmatter SkillLoader::parse_frontmatter(const std::string& content) {
    SkillFrontmatter fm;
    
    LOG_DEBUG("Parsing frontmatter from content (%zu bytes)", content.size());
    
    // Frontmatter is between --- and ---
    if (content.size() < 3 || content.substr(0, 3) != "---") {
        LOG_DEBUG("No frontmatter delimiter found at start");
        return fm;
    }
    
    size_t end_pos = content.find("\n---", 3);
    if (end_pos == std::string::npos) {
        LOG_DEBUG("No closing frontmatter delimiter found");
        return fm;
    }
    
    std::string frontmatter = content.substr(3, end_pos - 3);
    LOG_DEBUG("Extracted frontmatter (%zu bytes)", frontmatter.size());
    
    // Parse YAML-like frontmatter (simple key: value pairs)
    std::istringstream iss(frontmatter);
    std::string line;
    std::string current_key;
    std::string current_value;
    int brace_depth = 0;
    bool in_multiline = false;
    
    while (std::getline(iss, line)) {
        // Track brace depth for JSON-like values
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '{') brace_depth++;
            else if (line[i] == '}') brace_depth--;
        }
        
        if (in_multiline) {
            current_value += "\n" + line;
            if (brace_depth == 0) {
                fm[current_key] = trim(current_value);
                in_multiline = false;
                current_key.clear();
                current_value.clear();
            }
            continue;
        }
        
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = trim(line.substr(0, colon_pos));
            std::string value = trim(line.substr(colon_pos + 1));
            
            if (key.empty()) continue;
            
            // Handle multiline JSON-like values
            if (brace_depth > 0) {
                current_key = key;
                current_value = value;
                in_multiline = true;
                LOG_DEBUG("Starting multiline value for key: %s", key.c_str());
                continue;
            }
            
            fm[key] = value;
            LOG_DEBUG("Parsed frontmatter key: %s = %s", key.c_str(), value.c_str());
        }
    }
    
    LOG_DEBUG("Frontmatter parsing complete, %zu keys found", fm.size());
    return fm;
}

SkillMetadata SkillLoader::resolve_metadata(const SkillFrontmatter& frontmatter) {
    SkillMetadata metadata;
    
    LOG_DEBUG("Resolving metadata from frontmatter");
    
    if (frontmatter.count("metadata") == 0) {
        LOG_DEBUG("No metadata field found in frontmatter");
        return metadata;
    }
    
    LOG_DEBUG("Parsing metadata JSON: %s", frontmatter.at("metadata").c_str());
    bool success = parse_metadata_json(frontmatter.at("metadata"), metadata);
    LOG_DEBUG("Metadata parsing %s (always=%s, emoji=%s, skillKey=%s)",
              success ? "succeeded" : "failed",
              metadata.always ? "true" : "false",
              metadata.emoji.c_str(),
              metadata.skill_key.c_str());
    
    return metadata;
}

SkillInvocationPolicy SkillLoader::resolve_invocation_policy(const SkillFrontmatter& frontmatter) {
    SkillInvocationPolicy policy;
    
    LOG_DEBUG("Resolving invocation policy");
    
    if (frontmatter.count("user-invocable")) {
        policy.user_invocable = parse_bool(frontmatter.at("user-invocable"), true);
        LOG_DEBUG("Set user-invocable: %s", policy.user_invocable ? "true" : "false");
    }
    if (frontmatter.count("user_invocable")) {
        policy.user_invocable = parse_bool(frontmatter.at("user_invocable"), true);
        LOG_DEBUG("Set user_invocable: %s", policy.user_invocable ? "true" : "false");
    }
    if (frontmatter.count("disable-model-invocation")) {
        policy.disable_model_invocation = parse_bool(frontmatter.at("disable-model-invocation"), false);
        LOG_DEBUG("Set disable-model-invocation: %s", policy.disable_model_invocation ? "true" : "false");
    }
    if (frontmatter.count("disable_model_invocation")) {
        policy.disable_model_invocation = parse_bool(frontmatter.at("disable_model_invocation"), false);
        LOG_DEBUG("Set disable_model_invocation: %s", policy.disable_model_invocation ? "true" : "false");
    }
    
    LOG_DEBUG("Invocation policy: user_invocable=%s, disable_model_invocation=%s",
              policy.user_invocable ? "true" : "false",
              policy.disable_model_invocation ? "true" : "false");
    
    return policy;
}

SkillEntry SkillLoader::build_entry(const Skill& skill) {
    SkillEntry entry;
    entry.skill = skill;
    
    LOG_DEBUG("Building entry for skill: %s", skill.name.c_str());
    
    if (!skill.content.empty()) {
        LOG_DEBUG("Processing skill content for: %s", skill.name.c_str());
        entry.frontmatter = parse_frontmatter(skill.content);
        entry.metadata = resolve_metadata(entry.frontmatter);
        entry.invocation = resolve_invocation_policy(entry.frontmatter);
        LOG_DEBUG("Built complete entry for skill: %s", skill.name.c_str());
    } else {
        LOG_WARN(" Skill content is empty for: %s", skill.name.c_str());
    }
    
    return entry;
}

std::string SkillLoader::get_content_body(const std::string& content) {
    // Skip frontmatter
    if (content.size() < 3 || content.substr(0, 3) != "---") {
        return content;
    }
    
    size_t end_pos = content.find("\n---", 3);
    if (end_pos == std::string::npos) {
        return content;
    }
    
    // Find the actual end of the frontmatter line
    size_t body_start = content.find('\n', end_pos + 4);
    if (body_start == std::string::npos) {
        return "";
    }
    
    return content.substr(body_start + 1);
}

std::string SkillLoader::format_skill_for_prompt(const Skill& skill) {
    // Format: <available_skill name="name" location="path/to/SKILL.md">
    //   <description>...</description>
    // </available_skill>
    LOG_DEBUG("Formatting skill for AI prompt: %s", skill.name.c_str());
    
    std::ostringstream oss;
    oss << "<available_skill name=\"" << skill.name << "\" location=\"" << skill.file_path << "\">\n";
    oss << "  <description>" << skill.description << "</description>\n";
    oss << "</available_skill>";
    
    std::string result = oss.str();
    LOG_DEBUG("Formatted skill XML (%zu bytes) for: %s", result.size(), skill.name.c_str());
    return result;
}

bool SkillLoader::parse_metadata_json(const std::string& json_str, SkillMetadata& metadata) {
    // Simple JSON-like parsing for the metadata object
    // Looking for: { "opencrank": { ... } } or { "moltbot": { ... } }
    
    LOG_DEBUG("Parsing metadata JSON (%zu bytes)", json_str.size());
    
    // Find opencrank or moltbot key
    size_t opencrank_pos = json_str.find("\"opencrank\"");
    size_t moltbot_pos = json_str.find("\"moltbot\"");
    
    size_t key_pos = opencrank_pos;
    if (key_pos == std::string::npos || (moltbot_pos != std::string::npos && moltbot_pos < key_pos)) {
        key_pos = moltbot_pos;
    }
    
    if (key_pos == std::string::npos) {
        LOG_DEBUG("No opencrank or moltbot key found in metadata");
        return false;
    }
    
    LOG_DEBUG("Found metadata key at position %zu", key_pos);
    
    // Find the opening brace for the value
    size_t brace_start = json_str.find('{', key_pos);
    if (brace_start == std::string::npos) {
        return false;
    }
    
    // Find the matching closing brace
    int depth = 0;
    size_t brace_end = std::string::npos;
    for (size_t i = brace_start; i < json_str.size(); ++i) {
        if (json_str[i] == '{') depth++;
        else if (json_str[i] == '}') {
            depth--;
            if (depth == 0) {
                brace_end = i;
                break;
            }
        }
    }
    
    if (brace_end == std::string::npos) {
        return false;
    }
    
    std::string inner = json_str.substr(brace_start, brace_end - brace_start + 1);
    
    // Parse individual fields
    // "always": true/false
    size_t always_pos = inner.find("\"always\"");
    if (always_pos != std::string::npos) {
        size_t val_start = inner.find(':', always_pos);
        if (val_start != std::string::npos) {
            std::string val = trim(inner.substr(val_start + 1, 10));
            metadata.always = (val.find("true") == 0);
        }
    }
    
    // "emoji": "..."
    size_t emoji_pos = inner.find("\"emoji\"");
    if (emoji_pos != std::string::npos) {
        size_t quote_start = inner.find('"', emoji_pos + 7);
        if (quote_start != std::string::npos) {
            size_t quote_end = inner.find('"', quote_start + 1);
            if (quote_end != std::string::npos) {
                metadata.emoji = inner.substr(quote_start + 1, quote_end - quote_start - 1);
            }
        }
    }
    
    // "homepage": "..."
    size_t homepage_pos = inner.find("\"homepage\"");
    if (homepage_pos != std::string::npos) {
        size_t quote_start = inner.find('"', homepage_pos + 10);
        if (quote_start != std::string::npos) {
            size_t quote_end = inner.find('"', quote_start + 1);
            if (quote_end != std::string::npos) {
                metadata.homepage = inner.substr(quote_start + 1, quote_end - quote_start - 1);
            }
        }
    }
    
    // "skillKey": "..."
    size_t skillkey_pos = inner.find("\"skillKey\"");
    if (skillkey_pos != std::string::npos) {
        size_t quote_start = inner.find('"', skillkey_pos + 10);
        if (quote_start != std::string::npos) {
            size_t quote_end = inner.find('"', quote_start + 1);
            if (quote_end != std::string::npos) {
                metadata.skill_key = inner.substr(quote_start + 1, quote_end - quote_start - 1);
            }
        }
    }
    
    // "primaryEnv": "..."
    size_t primaryenv_pos = inner.find("\"primaryEnv\"");
    if (primaryenv_pos != std::string::npos) {
        size_t quote_start = inner.find('"', primaryenv_pos + 12);
        if (quote_start != std::string::npos) {
            size_t quote_end = inner.find('"', quote_start + 1);
            if (quote_end != std::string::npos) {
                metadata.primary_env = inner.substr(quote_start + 1, quote_end - quote_start - 1);
            }
        }
    }
    
    // "requires": { "bins": [...], "anyBins": [...], ... }
    size_t requires_pos = inner.find("\"requires\"");
    if (requires_pos != std::string::npos) {
        LOG_DEBUG("Found requirements section in metadata");
        size_t req_brace = inner.find('{', requires_pos);
        if (req_brace != std::string::npos) {
            int req_depth = 0;
            size_t req_end = std::string::npos;
            for (size_t i = req_brace; i < inner.size(); ++i) {
                if (inner[i] == '{') req_depth++;
                else if (inner[i] == '}') {
                    req_depth--;
                    if (req_depth == 0) {
                        req_end = i;
                        break;
                    }
                }
            }
            if (req_end != std::string::npos) {
                std::string req_str = inner.substr(req_brace, req_end - req_brace + 1);
                LOG_DEBUG("Parsing requirements: %s", req_str.c_str());
                parse_requirements(req_str, metadata.requirements);
            }
        }
    }
    
    LOG_DEBUG("Metadata JSON parsing complete");
    return true;
}

bool SkillLoader::parse_requirements(const std::string& json_str, SkillRequirements& reqs) {
    // Parse bins array
    auto parse_array = [&](const std::string& key, std::vector<std::string>& out) {
        size_t key_pos = json_str.find("\"" + key + "\"");
        if (key_pos == std::string::npos) return;
        
        size_t bracket_start = json_str.find('[', key_pos);
        if (bracket_start == std::string::npos) return;
        
        size_t bracket_end = json_str.find(']', bracket_start);
        if (bracket_end == std::string::npos) return;
        
        std::string arr = json_str.substr(bracket_start + 1, bracket_end - bracket_start - 1);
        
        // Parse quoted strings
        size_t pos = 0;
        while (pos < arr.size()) {
            size_t quote_start = arr.find('"', pos);
            if (quote_start == std::string::npos) break;
            
            size_t quote_end = arr.find('"', quote_start + 1);
            if (quote_end == std::string::npos) break;
            
            out.push_back(arr.substr(quote_start + 1, quote_end - quote_start - 1));
            pos = quote_end + 1;
        }
    };
    
    parse_array("bins", reqs.bins);
    parse_array("anyBins", reqs.any_bins);
    parse_array("env", reqs.env);
    parse_array("config", reqs.config);
    
    return true;
}

bool SkillLoader::parse_install_spec(const std::string& json_str, SkillInstallSpec& spec) {
    // Similar parsing for install spec objects
    // This is a simplified parser - production would use a proper JSON library
    
    auto get_string = [&](const std::string& key) -> std::string {
        size_t key_pos = json_str.find("\"" + key + "\"");
        if (key_pos == std::string::npos) return "";
        
        size_t quote_start = json_str.find('"', key_pos + key.size() + 2);
        if (quote_start == std::string::npos) return "";
        
        size_t quote_end = json_str.find('"', quote_start + 1);
        if (quote_end == std::string::npos) return "";
        
        return json_str.substr(quote_start + 1, quote_end - quote_start - 1);
    };
    
    spec.kind = get_string("kind");
    if (spec.kind.empty()) {
        spec.kind = get_string("type");
    }
    spec.id = get_string("id");
    spec.label = get_string("label");
    spec.formula = get_string("formula");
    spec.package = get_string("package");
    spec.module = get_string("module");
    spec.url = get_string("url");
    spec.archive = get_string("archive");
    spec.target_dir = get_string("targetDir");
    
    return !spec.kind.empty();
}

std::string SkillLoader::read_file(const std::string& path) {
    LOG_DEBUG("Reading file: %s", path.c_str());
    std::ifstream file(path.c_str());
    if (!file.is_open()) {
        LOG_ERROR(" Failed to open file: %s", path.c_str());
        set_error("Failed to open file: " + path);
        return "";
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    LOG_DEBUG("Successfully read %zu bytes from: %s", content.size(), path.c_str());
    return content;
}

bool SkillLoader::dir_exists(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

bool SkillLoader::file_exists(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode);
}

std::vector<std::string> SkillLoader::list_subdirs(const std::string& dir) {
    std::vector<std::string> subdirs;
    
    DIR* d = opendir(dir.c_str());
    if (!d) {
        return subdirs;
    }
    
    struct dirent* entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;  // Skip hidden and . / ..
        }
        
        std::string full_path = dir + "/" + entry->d_name;
        if (dir_exists(full_path)) {
            subdirs.push_back(entry->d_name);
        }
    }
    
    closedir(d);
    return subdirs;
}

std::string SkillLoader::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    
    size_t end = str.find_last_not_of(" \t\n\r");
    std::string result = str.substr(start, end - start + 1);
    
    // Also strip surrounding quotes (YAML values often have them)
    if (result.size() >= 2) {
        if ((result[0] == '"' && result[result.size() - 1] == '"') ||
            (result[0] == '\'' && result[result.size() - 1] == '\'')) {
            result = result.substr(1, result.size() - 2);
        }
    }
    
    return result;
}

std::string SkillLoader::to_lower(const std::string& str) {
    std::string result = str;
    for (size_t i = 0; i < result.size(); ++i) {
        result[i] = tolower(result[i]);
    }
    return result;
}

std::vector<std::string> SkillLoader::split(const std::string& str, char delim) {
    std::vector<std::string> parts;
    std::istringstream iss(str);
    std::string part;
    while (std::getline(iss, part, delim)) {
        std::string trimmed = trim(part);
        if (!trimmed.empty()) {
            parts.push_back(trimmed);
        }
    }
    return parts;
}

bool SkillLoader::parse_bool(const std::string& str, bool default_val) {
    std::string lower = to_lower(trim(str));
    if (lower == "true" || lower == "yes" || lower == "1" || lower == "on") {
        return true;
    }
    if (lower == "false" || lower == "no" || lower == "0" || lower == "off") {
        return false;
    }
    return default_val;
}

void SkillLoader::set_error(const std::string& err) {
    last_error_ = err;
}

// SkillEligibilityContext implementations
bool SkillEligibilityContext::Remote::has_bin(const std::string& bin) const {
    // Check if binary exists on the platform
    // In a real implementation, this would check actual binary availability
    for (size_t i = 0; i < platforms.size(); ++i) {
        if (platforms[i] == bin) {
            return true;
        }
    }
    return false;
}

bool SkillEligibilityContext::Remote::has_any_bin(const std::vector<std::string>& bins) const {
    for (size_t i = 0; i < bins.size(); ++i) {
        if (has_bin(bins[i])) {
            return true;
        }
    }
    return false;
}

} // namespace opencrank
