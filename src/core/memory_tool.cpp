/*
 * OpenCrank C++11 - Memory Tool Implementation
 * 
 * ToolProvider exposing memory, file, and task operations to the agent.
 * Memory and tasks are database-only (read/write to SQLite).
 * File operations use the workspace filesystem.
 */
#include <opencrank/core/memory_tool.hpp>
#include <opencrank/core/config.hpp>
#include <opencrank/core/logger.hpp>
#include <opencrank/core/sandbox.hpp>

#include <fstream>
#include <sstream>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <unistd.h>
#endif

namespace opencrank {

// ============================================================================
// Constructor / Destructor
// ============================================================================

MemoryTool::MemoryTool() : workspace_dir_(".") {}

MemoryTool::~MemoryTool() {
    shutdown();
}

// ============================================================================
// Plugin Interface
// ============================================================================

bool MemoryTool::init(const Config& cfg) {
    workspace_dir_ = cfg.get_string("workspace_dir", ".");
    
    if (!manager_.init(cfg)) {
        LOG_ERROR("[MemoryTool] Failed to initialize memory manager");
        return false;
    }
    
    initialized_ = true;
    LOG_INFO("[MemoryTool] Initialized (workspace=%s)", workspace_dir_.c_str());
    return true;
}

void MemoryTool::shutdown() {
    if (initialized_) {
        manager_.shutdown();
        initialized_ = false;
    }
}

// ============================================================================
// ToolProvider Interface
// ============================================================================

std::vector<std::string> MemoryTool::actions() const {
    std::vector<std::string> acts;
    acts.push_back("memory_save");
    acts.push_back("memory_search");
    acts.push_back("memory_get");
    acts.push_back("file_save");
    acts.push_back("file_read");
    acts.push_back("task_create");
    acts.push_back("task_list");
    acts.push_back("task_complete");
    return acts;
}

ToolResult MemoryTool::execute(const std::string& action, const Json& params) {
    if (!initialized_) {
        return ToolResult::fail("Memory tool not initialized");
    }
    
    if (action == "memory_save")   return do_memory_save(params);
    if (action == "memory_search") return do_memory_search(params);
    if (action == "memory_get")    return do_memory_get(params);
    if (action == "file_save")     return do_file_save(params);
    if (action == "file_read")     return do_file_read(params);
    if (action == "task_create")   return do_task_create(params);
    if (action == "task_list")     return do_task_list(params);
    if (action == "task_complete") return do_task_complete(params);
    
    return ToolResult::fail("Unknown action: " + action);
}

// ============================================================================
// Agent Tool Definitions
// ============================================================================

std::vector<AgentTool> MemoryTool::get_agent_tools() const {
    std::vector<AgentTool> tools;
    MemoryTool* self = const_cast<MemoryTool*>(this);
    
    // memory_save
    {
        AgentTool tool;
        tool.name = "memory_save";
        tool.description = 
            "Save important information to persistent memory database. "
            "Use this to remember facts, user preferences, decisions, "
            "conversation summaries, or anything that should persist across sessions. "
            "Memories are searchable via BM25 full-text search.";
        tool.params.push_back(ToolParamSchema(
            "content", "string",
            "The information to save. Be specific and include relevant context.",
            true
        ));
        tool.params.push_back(ToolParamSchema(
            "category", "string",
            "Category for organization (e.g., 'general', 'resume', 'note', 'preference', 'fact'). Default: 'general'",
            false
        ));
        tool.params.push_back(ToolParamSchema(
            "importance", "number",
            "Importance level 1-10. Higher values are prioritized in search. Default: 5",
            false
        ));
        tool.params.push_back(ToolParamSchema(
            "tags", "string",
            "Comma-separated tags for filtering (e.g., 'user,preference,language')",
            false
        ));
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult r = self->do_memory_save(params);
            return r.success 
                ? AgentToolResult::ok(r.data.contains("output") ? r.data["output"].get<std::string>() : "Memory saved")
                : AgentToolResult::fail(r.error);
        };
        tools.push_back(tool);
    }
    
    // memory_search
    {
        AgentTool tool;
        tool.name = "memory_search";
        tool.description = 
            "Search persistent memory using full-text search (BM25 ranking). "
            "Use this to recall past information, find saved notes, "
            "or look up things from previous conversations.";
        tool.params.push_back(ToolParamSchema(
            "query", "string",
            "Search query. Uses natural language keywords.",
            true
        ));
        tool.params.push_back(ToolParamSchema(
            "max_results", "number",
            "Maximum number of results to return (default: 5)",
            false
        ));
        tool.params.push_back(ToolParamSchema(
            "category", "string",
            "Filter by category (optional)",
            false
        ));
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult r = self->do_memory_search(params);
            return r.success 
                ? AgentToolResult::ok(r.data.contains("output") ? r.data["output"].get<std::string>() : "No results")
                : AgentToolResult::fail(r.error);
        };
        tools.push_back(tool);
    }
    
    // memory_get
    {
        AgentTool tool;
        tool.name = "memory_get";
        tool.description = 
            "Get a specific memory by ID, or list recent memories. "
            "Use without an ID to see what's been saved recently.";
        tool.params.push_back(ToolParamSchema(
            "id", "string",
            "Memory ID to retrieve. If omitted, returns recent entries.",
            false
        ));
        tool.params.push_back(ToolParamSchema(
            "limit", "number",
            "Number of recent entries to return (default: 5, only when id is not specified)",
            false
        ));
        tool.params.push_back(ToolParamSchema(
            "category", "string",
            "Filter by category (optional, only when id is not specified)",
            false
        ));
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult r = self->do_memory_get(params);
            return r.success 
                ? AgentToolResult::ok(r.data.contains("output") ? r.data["output"].get<std::string>() : "No results")
                : AgentToolResult::fail(r.error);
        };
        tools.push_back(tool);
    }
    
    // file_save
    {
        AgentTool tool;
        tool.name = "file_save";
        tool.description = 
            "Save content to a file in the workspace memory directory. "
            "Use for saving structured documents, notes, or daily logs. "
            "Files are saved under the memory/ directory by default.";
        tool.params.push_back(ToolParamSchema(
            "content", "string",
            "Content to write to the file",
            true
        ));
        tool.params.push_back(ToolParamSchema(
            "path", "string",
            "File path relative to workspace (default: auto-generated in memory/ directory)",
            false
        ));
        tool.params.push_back(ToolParamSchema(
            "daily", "boolean",
            "If true, saves to a daily file (memory/YYYY-MM-DD.md). Default: false",
            false
        ));
        tool.params.push_back(ToolParamSchema(
            "append", "boolean",
            "If true, append to existing file instead of overwriting. Default: false",
            false
        ));
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult r = self->do_file_save(params);
            return r.success 
                ? AgentToolResult::ok(r.data.contains("output") ? r.data["output"].get<std::string>() : "File saved")
                : AgentToolResult::fail(r.error);
        };
        tools.push_back(tool);
    }
    
    // file_read
    {
        AgentTool tool;
        tool.name = "file_read";
        tool.description = 
            "Read a file from the workspace memory directory.";
        tool.params.push_back(ToolParamSchema(
            "path", "string",
            "File path relative to workspace",
            true
        ));
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult r = self->do_file_read(params);
            return r.success 
                ? AgentToolResult::ok(r.data.contains("output") ? r.data["output"].get<std::string>() : "")
                : AgentToolResult::fail(r.error);
        };
        tools.push_back(tool);
    }
    
    // task_create
    {
        AgentTool tool;
        tool.name = "task_create";
        tool.description = 
            "Create a new task or reminder in the database. "
            "Tasks persist across sessions and can have optional due dates.";
        tool.params.push_back(ToolParamSchema(
            "content", "string",
            "Task description",
            true
        ));
        tool.params.push_back(ToolParamSchema(
            "context", "string",
            "Additional context or notes about the task",
            false
        ));
        tool.params.push_back(ToolParamSchema(
            "due_at", "number",
            "Due date as Unix timestamp in milliseconds (0 = no due date)",
            false
        ));
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult r = self->do_task_create(params);
            return r.success 
                ? AgentToolResult::ok(r.data.contains("output") ? r.data["output"].get<std::string>() : "Task created")
                : AgentToolResult::fail(r.error);
        };
        tools.push_back(tool);
    }
    
    // task_list
    {
        AgentTool tool;
        tool.name = "task_list";
        tool.description = 
            "List tasks from the database. By default shows only active (incomplete) tasks.";
        tool.params.push_back(ToolParamSchema(
            "include_completed", "boolean",
            "Include completed tasks (default: false)",
            false
        ));
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult r = self->do_task_list(params);
            return r.success 
                ? AgentToolResult::ok(r.data.contains("output") ? r.data["output"].get<std::string>() : "No tasks")
                : AgentToolResult::fail(r.error);
        };
        tools.push_back(tool);
    }
    
    // task_complete
    {
        AgentTool tool;
        tool.name = "task_complete";
        tool.description = 
            "Mark a task as completed by its ID.";
        tool.params.push_back(ToolParamSchema(
            "id", "string",
            "The task ID to mark as completed",
            true
        ));
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult r = self->do_task_complete(params);
            return r.success 
                ? AgentToolResult::ok(r.data.contains("output") ? r.data["output"].get<std::string>() : "Task completed")
                : AgentToolResult::fail(r.error);
        };
        tools.push_back(tool);
    }
    
    return tools;
}

// ============================================================================
// Helpers
// ============================================================================

std::string MemoryTool::resolve_path(const std::string& path) const {
    if (path.empty()) return workspace_dir_;
    
    // Absolute path
    if (path[0] == '/' || (path.size() > 1 && path[1] == ':')) {
        return path;
    }
    
    // Relative path
    if (workspace_dir_.empty() || workspace_dir_ == ".") {
        return path;
    }
    
    return workspace_dir_ + "/" + path;
}

bool MemoryTool::ensure_parent_dir(const std::string& filepath) const {
    // Find last '/' to get directory
    size_t pos = filepath.rfind('/');
    if (pos == std::string::npos) return true; // No directory component
    
    std::string dir = filepath.substr(0, pos);
    
    // Simple recursive mkdir
    std::string current;
    for (size_t i = 0; i < dir.size(); ++i) {
        current += dir[i];
        if (dir[i] == '/' || i == dir.size() - 1) {
            struct stat st;
            if (stat(current.c_str(), &st) != 0) {
                if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
                    return false;
                }
            }
        }
    }
    
    return true;
}

std::string MemoryTool::daily_file_path() const {
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d", tm_info);
    return "memory/" + std::string(buf) + ".md";
}

// ============================================================================
// Memory Actions (Database Read/Write)
// ============================================================================

ToolResult MemoryTool::do_memory_save(const Json& params) {
    if (!params.contains("content") || !params["content"].is_string()) {
        return ToolResult::fail("Missing required parameter: content");
    }
    
    std::string content = params["content"].get<std::string>();
    std::string category = "general";
    int importance = 5;
    std::string tags;
    
    if (params.contains("category") && params["category"].is_string()) {
        category = params["category"].get<std::string>();
    }
    if (params.contains("importance") && params["importance"].is_number()) {
        importance = params["importance"].get<int>();
    } else if (params.contains("importance") && params["importance"].is_string()) {
        try { importance = std::stoi(params["importance"].get<std::string>()); } 
        catch (...) {}
    }
    if (params.contains("tags") && params["tags"].is_string()) {
        tags = params["tags"].get<std::string>();
    }
    
    std::string result = manager_.save_memory(content, category, importance, tags);
    
    if (!result.empty()) {
        Json data;
        data["output"] = "Memory saved successfully (category: " + category + 
                          ", importance: " + std::to_string(importance) + ")";
        return ToolResult::ok(data);
    }
    
    return ToolResult::fail("Failed to save memory");
}

ToolResult MemoryTool::do_memory_search(const Json& params) {
    if (!params.contains("query") || !params["query"].is_string()) {
        return ToolResult::fail("Missing required parameter: query");
    }
    
    std::string query = params["query"].get<std::string>();
    int max_results = 5;
    std::string category;
    
    if (params.contains("max_results") && params["max_results"].is_number()) {
        max_results = params["max_results"].get<int>();
    } else if (params.contains("max_results") && params["max_results"].is_string()) {
        try { max_results = std::stoi(params["max_results"].get<std::string>()); }
        catch (...) {}
    }
    if (params.contains("category") && params["category"].is_string()) {
        category = params["category"].get<std::string>();
    }
    
    auto hits = manager_.search(query, max_results, category);
    
    // Build output text and structured data
    std::ostringstream out;
    Json data;
    Json memories = Json::array();
    
    if (hits.empty()) {
        out << "No memories found matching: " << query;
    } else {
        out << "Found " << hits.size() << " result(s) for: " << query << "\n\n";
        
        for (size_t i = 0; i < hits.size(); ++i) {
            const auto& hit = hits[i];
            
            out << "--- Result " << (i + 1) << " ---\n";
            out << "ID: " << hit.entry.id << "\n";
            out << "Category: " << hit.entry.category << "\n";
            if (!hit.entry.tags.empty()) {
                out << "Tags: " << hit.entry.tags << "\n";
            }
            out << "Content: " << hit.entry.content << "\n\n";
            
            // Structured data for programmatic access (used by context_manager)
            Json mem;
            mem["id"] = hit.entry.id;
            mem["content"] = hit.entry.content;
            mem["category"] = hit.entry.category;
            mem["tags"] = hit.entry.tags;
            mem["importance"] = hit.entry.importance;
            mem["score"] = hit.score;
            memories.push_back(mem);
        }
    }
    
    data["output"] = out.str();
    data["memories"] = memories;
    return ToolResult::ok(data);
}

ToolResult MemoryTool::do_memory_get(const Json& params) {
    // If ID is provided, get specific memory
    if (params.contains("id") && params["id"].is_string()) {
        std::string id = params["id"].get<std::string>();
        auto entry = manager_.get_memory(id);
        
        if (entry.id.empty()) {
            return ToolResult::fail("Memory not found: " + id);
        }
        
        std::ostringstream out;
        out << "ID: " << entry.id << "\n";
        out << "Category: " << entry.category << "\n";
        if (!entry.tags.empty()) {
            out << "Tags: " << entry.tags << "\n";
        }
        out << "Importance: " << entry.importance << "\n";
        out << "Content:\n" << entry.content;
        
        Json data;
        data["output"] = out.str();
        
        Json mem;
        mem["id"] = entry.id;
        mem["content"] = entry.content;
        mem["category"] = entry.category;
        mem["tags"] = entry.tags;
        mem["importance"] = entry.importance;
        
        Json memories = Json::array();
        memories.push_back(mem);
        data["memories"] = memories;
        
        return ToolResult::ok(data);
    }
    
    // Otherwise, get recent memories
    int limit = 5;
    std::string category;
    
    if (params.contains("limit") && params["limit"].is_number()) {
        limit = params["limit"].get<int>();
    }
    if (params.contains("category") && params["category"].is_string()) {
        category = params["category"].get<std::string>();
    }
    
    auto entries = manager_.get_recent(limit, category);
    
    std::ostringstream out;
    Json data;
    Json memories = Json::array();
    
    if (entries.empty()) {
        out << "No memories found.";
    } else {
        out << "Recent memories (" << entries.size() << "):\n\n";
        
        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& entry = entries[i];
            out << "--- " << (i + 1) << " ---\n";
            out << "ID: " << entry.id << "\n";
            out << "Category: " << entry.category << "\n";
            
            // Truncate long content for listing
            std::string preview = entry.content;
            if (preview.size() > 200) {
                preview = preview.substr(0, 200) + "...";
            }
            out << "Content: " << preview << "\n\n";
            
            Json mem;
            mem["id"] = entry.id;
            mem["content"] = entry.content;
            mem["category"] = entry.category;
            mem["tags"] = entry.tags;
            mem["importance"] = entry.importance;
            memories.push_back(mem);
        }
    }
    
    data["output"] = out.str();
    data["memories"] = memories;
    return ToolResult::ok(data);
}

// ============================================================================
// File Actions (Workspace Filesystem)
// ============================================================================

ToolResult MemoryTool::do_file_save(const Json& params) {
    if (!params.contains("content") || !params["content"].is_string()) {
        return ToolResult::fail("Missing required parameter: content");
    }
    
    std::string content = params["content"].get<std::string>();
    std::string path;
    bool daily = false;
    bool append = false;
    
    if (params.contains("daily")) {
        if (params["daily"].is_boolean()) {
            daily = params["daily"].get<bool>();
        } else if (params["daily"].is_string()) {
            daily = (params["daily"].get<std::string>() == "true");
        }
    }
    
    if (params.contains("append")) {
        if (params["append"].is_boolean()) {
            append = params["append"].get<bool>();
        } else if (params["append"].is_string()) {
            append = (params["append"].get<std::string>() == "true");
        }
    }
    
    if (daily) {
        path = daily_file_path();
    } else if (params.contains("path") && params["path"].is_string()) {
        path = params["path"].get<std::string>();
    } else {
        // Auto-generate a filename in memory/
        time_t now = time(nullptr);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", localtime(&now));
        path = "memory/" + std::string(buf) + ".md";
    }
    
    // Prevent directory traversal
    if (path.find("..") != std::string::npos) {
        return ToolResult::fail("Path not allowed: " + path);
    }
    
    std::string full_path = resolve_path(path);
    
    // Sandbox check
    auto& sandbox = Sandbox::instance();
    if (sandbox.is_active() && !sandbox.is_path_allowed(full_path)) {
        return ToolResult::fail("Path not allowed by sandbox: " + path);
    }
    
    // Ensure parent directory exists
    if (!ensure_parent_dir(full_path)) {
        return ToolResult::fail("Cannot create directory for: " + path);
    }
    
    // Open file
    std::ios_base::openmode mode = std::ios::out;
    if (append) {
        mode |= std::ios::app;
    }
    
    std::ofstream file(full_path.c_str(), mode);
    if (!file.is_open()) {
        return ToolResult::fail("Cannot open file for writing: " + path);
    }
    
    if (append) {
        file << "\n\n---\n\n";
    }
    file << content;
    file.close();
    
    LOG_DEBUG("[MemoryTool] File saved: %s (%zu bytes, append=%s)",
              path.c_str(), content.size(), append ? "true" : "false");
    
    Json data;
    data["output"] = "File saved: " + path + " (" + std::to_string(content.size()) + " bytes)";
    data["path"] = path;
    return ToolResult::ok(data);
}

ToolResult MemoryTool::do_file_read(const Json& params) {
    if (!params.contains("path") || !params["path"].is_string()) {
        return ToolResult::fail("Missing required parameter: path");
    }
    
    std::string path = params["path"].get<std::string>();
    
    // Prevent directory traversal
    if (path.find("..") != std::string::npos) {
        return ToolResult::fail("Path not allowed: " + path);
    }
    
    std::string full_path = resolve_path(path);
    
    // Sandbox check
    auto& sandbox = Sandbox::instance();
    if (sandbox.is_active() && !sandbox.is_path_allowed(full_path)) {
        return ToolResult::fail("Path not allowed by sandbox: " + path);
    }
    
    std::ifstream file(full_path.c_str());
    if (!file.is_open()) {
        return ToolResult::fail("Cannot open file: " + path);
    }
    
    std::ostringstream content;
    content << file.rdbuf();
    file.close();
    
    std::string result = content.str();
    
    // Truncate very large files
    constexpr size_t MAX_SIZE = 50000;
    if (result.size() > MAX_SIZE) {
        result = result.substr(0, MAX_SIZE) + "\n\n... [truncated, file too large] ...";
    }
    
    Json data;
    data["output"] = result;
    return ToolResult::ok(data);
}

// ============================================================================
// Task Actions (Database Read/Write)
// ============================================================================

ToolResult MemoryTool::do_task_create(const Json& params) {
    if (!params.contains("content") || !params["content"].is_string()) {
        return ToolResult::fail("Missing required parameter: content");
    }
    
    std::string content = params["content"].get<std::string>();
    std::string context;
    int64_t due_at = 0;
    std::string channel;
    std::string user_id;
    
    if (params.contains("context") && params["context"].is_string()) {
        context = params["context"].get<std::string>();
    }
    if (params.contains("due_at") && params["due_at"].is_number()) {
        due_at = params["due_at"].get<int64_t>();
    }
    if (params.contains("channel") && params["channel"].is_string()) {
        channel = params["channel"].get<std::string>();
    }
    if (params.contains("user_id") && params["user_id"].is_string()) {
        user_id = params["user_id"].get<std::string>();
    }
    
    std::string result = manager_.create_task(content, context, due_at, channel, user_id);
    
    if (!result.empty()) {
        std::ostringstream out;
        out << "Task created: " << content;
        if (due_at > 0) {
            time_t due_sec = static_cast<time_t>(due_at / 1000);
            char buf[64];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", localtime(&due_sec));
            out << " (due: " << buf << ")";
        }
        
        Json data;
        data["output"] = out.str();
        return ToolResult::ok(data);
    }
    
    return ToolResult::fail("Failed to create task");
}

ToolResult MemoryTool::do_task_list(const Json& params) {
    bool include_completed = false;
    std::string channel;
    
    if (params.contains("include_completed")) {
        if (params["include_completed"].is_boolean()) {
            include_completed = params["include_completed"].get<bool>();
        } else if (params["include_completed"].is_string()) {
            include_completed = (params["include_completed"].get<std::string>() == "true");
        }
    }
    if (params.contains("channel") && params["channel"].is_string()) {
        channel = params["channel"].get<std::string>();
    }
    
    auto tasks = manager_.list_tasks(include_completed, channel);
    
    std::ostringstream out;
    
    if (tasks.empty()) {
        out << "No tasks found.";
    } else {
        // Check for overdue
        auto due_tasks = manager_.get_due_tasks();
        
        if (!due_tasks.empty()) {
            out << "⚠ OVERDUE TASKS (" << due_tasks.size() << "):\n";
            for (size_t i = 0; i < due_tasks.size(); ++i) {
                time_t due_sec = static_cast<time_t>(due_tasks[i].due_at / 1000);
                char buf[64];
                strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", localtime(&due_sec));
                out << "  [" << due_tasks[i].id.substr(0, 8) << "] " 
                    << due_tasks[i].content << " (due: " << buf << ")\n";
            }
            out << "\n";
        }
        
        out << "Tasks (" << tasks.size() << "):\n\n";
        
        for (size_t i = 0; i < tasks.size(); ++i) {
            const auto& task = tasks[i];
            
            out << (task.completed ? "[✓] " : "[ ] ");
            out << "[" << task.id.substr(0, 8) << "] ";
            out << task.content;
            
            if (task.due_at > 0) {
                time_t due_sec = static_cast<time_t>(task.due_at / 1000);
                char buf[64];
                strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", localtime(&due_sec));
                out << " (due: " << buf << ")";
            }
            
            if (!task.context.empty()) {
                out << "\n    Notes: " << task.context;
            }
            
            out << "\n";
        }
    }
    
    Json data;
    data["output"] = out.str();
    return ToolResult::ok(data);
}

ToolResult MemoryTool::do_task_complete(const Json& params) {
    if (!params.contains("id") || !params["id"].is_string()) {
        return ToolResult::fail("Missing required parameter: id");
    }
    
    std::string id = params["id"].get<std::string>();
    
    // Try to find the task first (support short IDs)
    auto task = manager_.get_task(id);
    
    // If exact match not found, try to match by prefix
    if (task.id.empty()) {
        auto all_tasks = manager_.list_tasks(false);
        for (size_t i = 0; i < all_tasks.size(); ++i) {
            if (all_tasks[i].id.substr(0, id.size()) == id) {
                task = all_tasks[i];
                id = task.id; // Use full ID
                break;
            }
        }
    }
    
    if (task.id.empty()) {
        return ToolResult::fail("Task not found: " + id);
    }
    
    if (task.completed) {
        Json data;
        data["output"] = "Task already completed: " + task.content;
        return ToolResult::ok(data);
    }
    
    if (manager_.complete_task(id)) {
        Json data;
        data["output"] = "Task completed: " + task.content;
        return ToolResult::ok(data);
    }
    
    return ToolResult::fail("Failed to complete task: " + id);
}

} // namespace opencrank
