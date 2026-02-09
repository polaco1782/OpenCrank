/*
 * OpenCrank C++11 - Built-in Tools Provider Implementation
 * 
 * BuiltinToolsProvider class: filesystem and shell tools for the agent.
 * Legacy factory functions are in builtin_tools_legacy.cpp.
 */
#include <opencrank/core/builtin_tools.hpp>
#include <opencrank/core/logger.hpp>
#include <opencrank/core/config.hpp>

#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <sys/stat.h>
#include <dirent.h>

#ifdef _WIN32
#include <windows.h>
#define popen _popen
#define pclose _pclose
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace opencrank {

// ============================================================================
// BuiltinToolsProvider Implementation
// ============================================================================

BuiltinToolsProvider::BuiltinToolsProvider()
    : workspace_dir_(".")
    , shell_timeout_(20)
    , chunker_(nullptr) {}

BuiltinToolsProvider::~BuiltinToolsProvider() {
    shutdown();
}

bool BuiltinToolsProvider::init(const Config& cfg) {
    workspace_dir_ = cfg.get_string("workspace_dir", ".");
    shell_timeout_ = static_cast<int>(cfg.get_int("agent.shell_timeout", 20));
    
    LOG_INFO("Builtin tools initialized (workspace=%s, shell_timeout=%ds)",
             workspace_dir_.c_str(), shell_timeout_);
    
    initialized_ = true;
    return true;
}

void BuiltinToolsProvider::shutdown() {
    initialized_ = false;
}

std::vector<std::string> BuiltinToolsProvider::actions() const {
    std::vector<std::string> acts;
    acts.push_back("read");
    acts.push_back("write");
    acts.push_back("shell");
    acts.push_back("list_dir");
    acts.push_back("content_chunk");
    acts.push_back("content_search");
    return acts;
}

ToolResult BuiltinToolsProvider::execute(const std::string& action, const Json& params) {
    AgentToolResult result;
    
    if (action == "read") {
        result = do_read(params);
    } else if (action == "write") {
        result = do_write(params);
    } else if (action == "shell") {
        result = do_shell(params);
    } else if (action == "list_dir") {
        result = do_list_dir(params);
    } else if (action == "content_chunk") {
        result = do_content_chunk(params);
    } else if (action == "content_search") {
        result = do_content_search(params);
    } else {
        return ToolResult::fail("Unknown action: " + action);
    }
    
    if (result.success) {
        Json data;
        data["output"] = result.output;
        return ToolResult::ok(data);
    }
    return ToolResult::fail(result.error);
}

std::vector<AgentTool> BuiltinToolsProvider::get_agent_tools() const {
    std::vector<AgentTool> tools;
    BuiltinToolsProvider* self = const_cast<BuiltinToolsProvider*>(this);
    
    // read - Read file contents
    {
        AgentTool tool;
        tool.name = "read";
        tool.description = "Read the contents of a file. Use this to examine files, "
                           "read documentation, or load skill instructions.";
        tool.params.push_back(ToolParamSchema(
            "path", "string", 
            "Path to the file to read (relative to workspace)", 
            true
        ));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            return self->do_read(params);
        };
        
        tools.push_back(tool);
    }
    
    // write - Write content to files
    {
        AgentTool tool;
        tool.name = "write";
        tool.description = "Write content to a file. Creates the file if it doesn't exist, "
                           "overwrites if it does.";
        tool.params.push_back(ToolParamSchema(
            "path", "string", 
            "Path to the file (relative to workspace)", 
            true
        ));
        tool.params.push_back(ToolParamSchema(
            "content", "string", 
            "Content to write to the file", 
            true
        ));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            return self->do_write(params);
        };
        
        tools.push_back(tool);
    }
    
    // shell - Execute shell commands
    {
        AgentTool tool;
        tool.name = "shell";
        tool.description = "Execute a shell command and return its output. "
                           "Use this for running scripts, checking system state, or executing programs.";
        tool.params.push_back(ToolParamSchema(
            "command", "string", 
            "The shell command to execute", 
            true
        ));
        tool.params.push_back(ToolParamSchema(
            "workdir", "string", 
            "Working directory (optional)", 
            false
        ));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            return self->do_shell(params);
        };
        
        tools.push_back(tool);
    }
    
    // list_dir - List directory contents
    {
        AgentTool tool;
        tool.name = "list_dir";
        tool.description = "List the contents of a directory.";
        tool.params.push_back(ToolParamSchema(
            "path", "string", 
            "Path to the directory (relative to workspace)", 
            false
        ));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            return self->do_list_dir(params);
        };
        
        tools.push_back(tool);
    }
    
    // content_chunk - Retrieve chunks of large content
    {
        AgentTool tool;
        tool.name = "content_chunk";
        tool.description = "Retrieve a specific chunk of large content that was stored due to size limits. "
                           "Use this when tool results indicate content was chunked.";
        tool.params.push_back(ToolParamSchema(
            "id", "string", 
            "The content ID (e.g., 'chunk_1')", 
            true
        ));
        tool.params.push_back(ToolParamSchema(
            "chunk", "number", 
            "Chunk index (0-based)", 
            true
        ));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            return self->do_content_chunk(params);
        };
        
        tools.push_back(tool);
    }
    
    // content_search - Search within large content
    {
        AgentTool tool;
        tool.name = "content_search";
        tool.description = "Search for text within large stored content. Returns matching excerpts with context. "
                           "Use this to find specific information without reading all chunks.";
        tool.params.push_back(ToolParamSchema(
            "id", "string", 
            "The content ID (e.g., 'chunk_1')", 
            true
        ));
        tool.params.push_back(ToolParamSchema(
            "query", "string", 
            "Text to search for (case-insensitive)", 
            true
        ));
        tool.params.push_back(ToolParamSchema(
            "context", "number", 
            "Characters of context around each match (default: 500)", 
            false
        ));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            return self->do_content_search(params);
        };
        
        tools.push_back(tool);
    }
    
    return tools;
}

// ============================================================================
// BuiltinToolsProvider - Internal Implementations
// ============================================================================

AgentToolResult BuiltinToolsProvider::do_read(const Json& params) const {
    if (!params.contains("path") || !params["path"].is_string()) {
        return AgentToolResult::fail("Missing required parameter: path");
    }
    
    auto file_path = params["path"].get<std::string>();
    auto full_path = builtin_tools::path::resolve(file_path, workspace_dir_);
    
    LOG_DEBUG("[read tool] Reading file: %s", full_path.c_str());
    
    if (!builtin_tools::path::is_within_workspace(full_path, workspace_dir_)) {
        return AgentToolResult::fail("Path not allowed: " + file_path);
    }
    
    std::ifstream file(full_path.c_str());
    if (!file.is_open()) {
        return AgentToolResult::fail("Cannot open file: " + file_path);
    }
    
    std::ostringstream content;
    content << file.rdbuf();
    file.close();
    
    auto result = content.str();
    
    // Truncate very large files
    constexpr size_t MAX_SIZE = 50000;
    if (result.size() > MAX_SIZE) {
        result = result.substr(0, MAX_SIZE) + "\n\n... [truncated, file too large] ...";
    }
    
    return AgentToolResult::ok(result);
}

AgentToolResult BuiltinToolsProvider::do_write(const Json& params) const {
    if (!params.contains("path") || !params["path"].is_string()) {
        return AgentToolResult::fail("Missing required parameter: path");
    }
    if (!params.contains("content") || !params["content"].is_string()) {
        return AgentToolResult::fail("Missing required parameter: content");
    }
    
    auto file_path = params["path"].get<std::string>();
    auto content = params["content"].get<std::string>();
    auto full_path = builtin_tools::path::resolve(file_path, workspace_dir_);
    
    if (!builtin_tools::path::is_within_workspace(full_path, workspace_dir_)) {
        return AgentToolResult::fail("Path not allowed: " + file_path);
    }
    
    LOG_DEBUG("[write tool] Writing file: %s (%zu bytes)", 
              full_path.c_str(), content.size());
    
    std::ofstream file(full_path.c_str());
    if (!file.is_open()) {
        return AgentToolResult::fail("Cannot open file for writing: " + file_path);
    }
    
    file << content;
    file.close();
    
    return AgentToolResult::ok(
        "Successfully wrote " + std::to_string(content.size()) + " bytes to " + file_path
    );
}

AgentToolResult BuiltinToolsProvider::do_shell(const Json& params) const {
    if (!params.contains("command") || !params["command"].is_string()) {
        return AgentToolResult::fail("Missing required parameter: command");
    }
    
    auto command = params["command"].get<std::string>();
    auto workdir = workspace_dir_;
    
    if (params.contains("workdir") && params["workdir"].is_string()) {
        workdir = builtin_tools::path::resolve(params["workdir"].get<std::string>(), workspace_dir_);
    }
    
    // Auto-add timeout to curl commands
    if (command.find("curl ") != std::string::npos && 
        command.find("--connect-timeout") == std::string::npos &&
        command.find("-m ") == std::string::npos &&
        command.find("--max-time") == std::string::npos) {
        
        auto curl_pos = command.find("curl ");
        command = command.substr(0, curl_pos + 5) + 
                  "--connect-timeout 10 --max-time 15 " + 
                  command.substr(curl_pos + 5);
        LOG_DEBUG("[shell tool] Auto-added timeout to curl: %s", command.c_str());
    }
    
    LOG_INFO("[shell tool] Executing: %s (in %s)", command.c_str(), workdir.c_str());
    
    // Security: Block dangerous patterns
    std::string lower_cmd = command;
    for (auto& c : lower_cmd) {
        c = std::tolower(static_cast<unsigned char>(c));
    }
    
    if (lower_cmd.find("rm -rf /") != std::string::npos ||
        lower_cmd.find("rm -rf ~") != std::string::npos ||
        lower_cmd.find(":(){") != std::string::npos) {  // Fork bomb
        return AgentToolResult::fail("Command blocked for safety");
    }
    
    // Build full command
    std::ostringstream full_cmd;
    full_cmd << "cd \"" << workdir << "\" && ";
    
#ifndef _WIN32
    if (shell_timeout_ > 0) {
        full_cmd << "timeout " << shell_timeout_ << " ";
    }
#endif
    
    full_cmd << command << " 2>&1";
    
    // Execute
    FILE* pipe = popen(full_cmd.str().c_str(), "r");
    if (!pipe) {
        return AgentToolResult::fail("Failed to execute command");
    }
    
    std::ostringstream output;
    char buffer[4096];
    size_t total_read = 0;
    constexpr size_t MAX_OUTPUT = 100000;
    
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr && total_read < MAX_OUTPUT) {
        output << buffer;
        total_read += strlen(buffer);
    }
    
    int status = pclose(pipe);
    
    auto result = output.str();
    if (total_read >= MAX_OUTPUT) {
        result += "\n... [output truncated] ...";
    }
    
#ifndef _WIN32
    int exit_code = WEXITSTATUS(status);
#else
    int exit_code = status;
#endif
    
    if (exit_code != 0) {
        std::ostringstream err;
        if (exit_code == 124) {
            // Timeout
            err << "Command timed out after " << shell_timeout_ << " seconds.";
            if (!result.empty()) {
                err << " Partial output:\n" << result;
            }
            err << "\nTry an alternative approach or different service.";
        } else {
            err << "Command exited with code " << exit_code;
            if (!result.empty()) {
                err << ":\n" << result;
            }
        }
        // Return as success so AI can see output and retry
        return AgentToolResult::ok(err.str());
    }
    
    if (result.empty()) {
        result = "(no output)";
    }
    
    return AgentToolResult::ok(result);
}

AgentToolResult BuiltinToolsProvider::do_list_dir(const Json& params) const {
    std::string dir_path = ".";
    if (params.contains("path") && params["path"].is_string()) {
        dir_path = params["path"].get<std::string>();
    }
    
    auto full_path = builtin_tools::path::resolve(dir_path, workspace_dir_);
    
    if (!builtin_tools::path::is_within_workspace(full_path, workspace_dir_)) {
        return AgentToolResult::fail("Path not allowed: " + dir_path);
    }
    
    DIR* dir = opendir(full_path.c_str());
    if (!dir) {
        return AgentToolResult::fail("Cannot open directory: " + dir_path);
    }
    
    std::ostringstream result;
    result << "Contents of " << dir_path << ":\n";
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        
        auto entry_path = full_path + "/" + name;
        struct stat st;
        if (stat(entry_path.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                result << "  " << name << "/\n";
            } else {
                result << "  " << name << " (" << st.st_size << " bytes)\n";
            }
        } else {
            result << "  " << name << "\n";
        }
    }
    
    closedir(dir);
    return AgentToolResult::ok(result.str());
}

AgentToolResult BuiltinToolsProvider::do_content_chunk(const Json& params) const {
    if (!chunker_) {
        return AgentToolResult::fail("Content chunker not available");
    }
    
    if (!params.contains("id") || !params["id"].is_string()) {
        return AgentToolResult::fail("Missing required parameter: id");
    }
    
    auto id = params["id"].get<std::string>();
    size_t chunk_index = 0;
    
    if (params.contains("chunk")) {
        if (params["chunk"].is_number()) {
            chunk_index = static_cast<size_t>(params["chunk"].get<int>());
        } else if (params["chunk"].is_string()) {
            try {
                chunk_index = static_cast<size_t>(std::stoi(params["chunk"].get<std::string>()));
            } catch (...) {
                return AgentToolResult::fail("Invalid chunk index: must be a number");
            }
        }
    }
    
    LOG_DEBUG("[content_chunk tool] Retrieving chunk %zu of '%s'", chunk_index, id.c_str());
    
    if (!chunker_->has(id)) {
        return AgentToolResult::fail(
            "Content ID '" + id + "' not found. It may have expired or been cleared."
        );
    }
    
    auto chunk = chunker_->get_chunk(id, chunk_index);
    return AgentToolResult::ok(chunk);
}

AgentToolResult BuiltinToolsProvider::do_content_search(const Json& params) const {
    if (!chunker_) {
        return AgentToolResult::fail("Content chunker not available");
    }
    
    if (!params.contains("id") || !params["id"].is_string()) {
        return AgentToolResult::fail("Missing required parameter: id");
    }
    if (!params.contains("query") || !params["query"].is_string()) {
        return AgentToolResult::fail("Missing required parameter: query");
    }
    
    auto id = params["id"].get<std::string>();
    auto query = params["query"].get<std::string>();
    size_t context_chars = 500;
    
    if (params.contains("context")) {
        if (params["context"].is_number()) {
            context_chars = static_cast<size_t>(params["context"].get<int>());
        } else if (params["context"].is_string()) {
            try {
                context_chars = static_cast<size_t>(std::stoi(params["context"].get<std::string>()));
            } catch (...) {
                // Use default
            }
        }
    }
    
    LOG_DEBUG("[content_search tool] Searching for '%s' in '%s'", query.c_str(), id.c_str());
    
    if (!chunker_->has(id)) {
        return AgentToolResult::fail(
            "Content ID '" + id + "' not found. It may have expired or been cleared."
        );
    }
    
    auto result = chunker_->search(id, query, context_chars);
    return AgentToolResult::ok(result);
}

} // namespace opencrank
