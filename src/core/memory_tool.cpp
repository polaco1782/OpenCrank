/*
 * OpenCrank C++11 - Memory Tool (Core) Implementation
 *
 * Clean separation:
 *   file_*   actions → FILE operations (read/write/list memory files)
 *   memory_* actions → DATABASE operations (structured memory entries in SQLite)
 *   task_*   actions → DATABASE operations (task/reminder management in SQLite)
 */
#include <opencrank/core/memory_tool.hpp>
#include <opencrank/core/registry.hpp>
#include <sstream>

namespace opencrank {

MemoryTool::MemoryTool() {}

MemoryTool::~MemoryTool() {
    shutdown();
}

const char* MemoryTool::name() const {
    return "memory";
}

const char* MemoryTool::description() const {
    return "Memory and task management for persistent context";
}

const char* MemoryTool::version() const {
    return "1.0.0";
}

const char* MemoryTool::tool_id() const {
    return "memory";
}

std::vector<std::string> MemoryTool::actions() const {
    std::vector<std::string> acts;
    // File operations (filesystem)
    acts.push_back("file_save");
    acts.push_back("file_get");
    acts.push_back("file_list");
    // Memory operations (database)
    acts.push_back("memory_save");
    acts.push_back("memory_search");
    acts.push_back("memory_list");
    acts.push_back("memory_delete");
    // Task operations (database)
    acts.push_back("task_create");
    acts.push_back("task_complete");
    acts.push_back("task_list");
    return acts;
}

std::vector<AgentTool> MemoryTool::get_agent_tools() const {
    std::vector<AgentTool> tools;
    ToolProvider* self = const_cast<MemoryTool*>(this);
    
    // ================================================================
    // FILE operations - read/write/list memory files on disk
    // ================================================================
    
    // file_save - Write content to a memory file on disk
    {
        AgentTool tool;
        tool.name = "file_save";
        tool.description = "Write content to a memory file on disk. Use for markdown notes, daily logs, or any text file in the memory workspace.";
        tool.params.push_back(ToolParamSchema("content", "string", "The content to write to the file", true));
        tool.params.push_back(ToolParamSchema("filename", "string", "Optional filename (default: MEMORY.md)", false));
        tool.params.push_back(ToolParamSchema("daily", "boolean", "If true, save to daily file (memory/YYYY-MM-DD.md)", false));
        tool.params.push_back(ToolParamSchema("append", "boolean", "If true, append to existing file instead of overwriting", false));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult result = self->execute("file_save", params);
            if (!result.success) {
                return AgentToolResult::fail(result.error);
            }
            std::string msg = result.data.value("message", std::string("File saved"));
            return AgentToolResult::ok(msg);
        };
        
        tools.push_back(tool);
    }
    
    // file_get - Read the content of a memory file from disk
    {
        AgentTool tool;
        tool.name = "file_get";
        tool.description = "Read the full content of a specific memory file from disk.";
        tool.params.push_back(ToolParamSchema("path", "string", "Path to the memory file (e.g., 'MEMORY.md' or 'memory/2024-01-15.md')", true));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult result = self->execute("file_get", params);
            if (!result.success) {
                return AgentToolResult::fail(result.error);
            }
            if (result.data.contains("content") && result.data["content"].is_string()) {
                return AgentToolResult::ok(result.data["content"].get<std::string>());
            }
            return AgentToolResult::ok(result.data.dump(2));
        };
        
        tools.push_back(tool);
    }
    
    // file_list - List all memory files on disk
    {
        AgentTool tool;
        tool.name = "file_list";
        tool.description = "List all memory files on disk in the workspace.";
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult result = self->execute("file_list", params);
            if (!result.success) {
                return AgentToolResult::fail(result.error);
            }
            return AgentToolResult::ok(result.data.dump(2));
        };
        
        tools.push_back(tool);
    }
    
    // ================================================================
    // MEMORY operations - structured entries in SQLite database
    // ================================================================
    
    // memory_save - Save a structured memory entry to the database
    {
        AgentTool tool;
        tool.name = "memory_save";
        tool.description = "Save a structured memory entry to the database. Use for facts, preferences, or context that should persist across sessions. This is DATABASE storage, not file storage.";
        tool.params.push_back(ToolParamSchema("content", "string", "The content to save to memory", true));
        tool.params.push_back(ToolParamSchema("category", "string", "Category: general, resume, fact, preference (default: general)", false));
        tool.params.push_back(ToolParamSchema("importance", "number", "Importance 1-10 (default: 5)", false));
        tool.params.push_back(ToolParamSchema("tags", "string", "Comma-separated tags for search", false));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult result = self->execute("memory_save", params);
            if (!result.success) {
                return AgentToolResult::fail(result.error);
            }
            std::string msg = result.data.value("message", std::string("Memory saved"));
            return AgentToolResult::ok(msg);
        };
        
        tools.push_back(tool);
    }
    
    // memory_search - Search structured memories in the database
    {
        AgentTool tool;
        tool.name = "memory_search";
        tool.description = "Search through structured memory entries in the database. Returns entries ranked by importance and relevance.";
        tool.params.push_back(ToolParamSchema("query", "string", "Search query - keywords or natural language", true));
        tool.params.push_back(ToolParamSchema("max_results", "number", "Maximum number of results (default: 10)", false));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult result = self->execute("memory_search", params);
            if (!result.success) {
                return AgentToolResult::fail(result.error);
            }
            return AgentToolResult::ok(result.data.dump(2));
        };
        
        tools.push_back(tool);
    }
    
    // memory_list - List structured memory entries from the database
    {
        AgentTool tool;
        tool.name = "memory_list";
        tool.description = "List structured memory entries from the database, optionally filtered by category.";
        tool.params.push_back(ToolParamSchema("category", "string", "Filter by category (empty = all)", false));
        tool.params.push_back(ToolParamSchema("limit", "number", "Maximum entries to return (default: 100)", false));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult result = self->execute("memory_list", params);
            if (!result.success) {
                return AgentToolResult::fail(result.error);
            }
            return AgentToolResult::ok(result.data.dump(2));
        };
        
        tools.push_back(tool);
    }
    
    // memory_delete - Delete a structured memory entry from the database
    {
        AgentTool tool;
        tool.name = "memory_delete";
        tool.description = "Delete a structured memory entry from the database by its ID.";
        tool.params.push_back(ToolParamSchema("id", "string", "The memory entry ID to delete", true));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult result = self->execute("memory_delete", params);
            if (!result.success) {
                return AgentToolResult::fail(result.error);
            }
            std::string msg = result.data.value("message", std::string("Memory deleted"));
            return AgentToolResult::ok(msg);
        };
        
        tools.push_back(tool);
    }
    
    // ================================================================
    // TASK operations - task/reminder management in SQLite database
    // ================================================================
    
    // task_create - Create a task or reminder
    {
        AgentTool tool;
        tool.name = "task_create";
        tool.description = "Create a task or reminder for later. Tasks persist across sessions in the database.";
        tool.params.push_back(ToolParamSchema("content", "string", "The task description", true));
        tool.params.push_back(ToolParamSchema("context", "string", "Additional context or notes", false));
        tool.params.push_back(ToolParamSchema("due_at", "number", "Due date as Unix timestamp in milliseconds (0 = no due date)", false));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult result = self->execute("task_create", params);
            if (!result.success) {
                return AgentToolResult::fail(result.error);
            }
            return AgentToolResult::ok(result.data.dump(2));
        };
        
        tools.push_back(tool);
    }
    
    // task_complete - Mark a task as completed
    {
        AgentTool tool;
        tool.name = "task_complete";
        tool.description = "Mark a task as completed by its ID.";
        tool.params.push_back(ToolParamSchema("task_id", "string", "The task ID to mark as complete", true));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult result = self->execute("task_complete", params);
            if (!result.success) {
                return AgentToolResult::fail(result.error);
            }
            std::string msg = result.data.value("message", std::string("Task completed"));
            return AgentToolResult::ok(msg);
        };
        
        tools.push_back(tool);
    }
    
    // task_list - List all tasks
    {
        AgentTool tool;
        tool.name = "task_list";
        tool.description = "List all tasks from the database. Shows pending tasks by default.";
        tool.params.push_back(ToolParamSchema("include_completed", "boolean", "Whether to include completed tasks (default: false)", false));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult result = self->execute("task_list", params);
            if (!result.success) {
                return AgentToolResult::fail(result.error);
            }
            return AgentToolResult::ok(result.data.dump(2));
        };
        
        tools.push_back(tool);
    }
    
    return tools;
}

bool MemoryTool::init(const Config& cfg) {
    if (initialized_) {
        return true;
    }
    
    config_.workspace_dir = cfg.get_string("workspace_dir", ".");
    config_.db_path = cfg.get_string("memory_db_path", "");
    config_.chunking.target_tokens = cfg.get_int("memory_chunk_tokens", 400);
    config_.chunking.overlap_tokens = cfg.get_int("memory_chunk_overlap", 80);
    config_.search.max_results = cfg.get_int("memory_max_results", 10);
    config_.search.min_score = 0.1;
    
    manager_.reset(new MemoryManager(config_));
    
    if (!manager_->initialize()) {
        return false;
    }
    
    manager_->sync();
    
    initialized_ = true;
    return true;
}

void MemoryTool::shutdown() {
    if (manager_) {
        manager_->shutdown();
        manager_.reset();
    }
    initialized_ = false;
}

Json MemoryTool::get_tool_schema() const {
    Json tools = Json::array();
    
    // ================================================================
    // FILE operations schema
    // ================================================================
    
    // file_save
    {
        Json tool;
        tool["name"] = "file_save";
        tool["description"] = "Write content to a memory file on disk.";
        
        Json params;
        params["type"] = "object";
        
        Json props;
        { Json p; p["type"] = "string"; p["description"] = "The content to write to the file"; props["content"] = p; }
        { Json p; p["type"] = "string"; p["description"] = "Optional filename (default: MEMORY.md)"; props["filename"] = p; }
        { Json p; p["type"] = "boolean"; p["description"] = "If true, save to daily file (memory/YYYY-MM-DD.md)"; props["daily"] = p; }
        { Json p; p["type"] = "boolean"; p["description"] = "If true, append instead of overwriting"; props["append"] = p; }
        
        params["properties"] = props;
        Json required = Json::array();
        required.push_back("content");
        params["required"] = required;
        tool["parameters"] = params;
        tools.push_back(tool);
    }
    
    // file_get
    {
        Json tool;
        tool["name"] = "file_get";
        tool["description"] = "Read the full content of a specific memory file from disk.";
        
        Json params;
        params["type"] = "object";
        Json props;
        { Json p; p["type"] = "string"; p["description"] = "Path to the memory file (e.g., 'MEMORY.md')"; props["path"] = p; }
        params["properties"] = props;
        Json required = Json::array();
        required.push_back("path");
        params["required"] = required;
        tool["parameters"] = params;
        tools.push_back(tool);
    }
    
    // file_list
    {
        Json tool;
        tool["name"] = "file_list";
        tool["description"] = "List all memory files on disk.";
        Json params;
        params["type"] = "object";
        params["properties"] = Json::object();
        tool["parameters"] = params;
        tools.push_back(tool);
    }
    
    // ================================================================
    // MEMORY operations schema (DATABASE)
    // ================================================================
    
    // memory_save
    {
        Json tool;
        tool["name"] = "memory_save";
        tool["description"] = "Save a structured memory entry to the database.";
        
        Json params;
        params["type"] = "object";
        Json props;
        { Json p; p["type"] = "string"; p["description"] = "The content to save"; props["content"] = p; }
        { Json p; p["type"] = "string"; p["description"] = "Category: general, resume, fact, preference"; props["category"] = p; }
        { Json p; p["type"] = "integer"; p["description"] = "Importance 1-10 (default: 5)"; props["importance"] = p; }
        { Json p; p["type"] = "string"; p["description"] = "Comma-separated tags for search"; props["tags"] = p; }
        params["properties"] = props;
        Json required = Json::array();
        required.push_back("content");
        params["required"] = required;
        tool["parameters"] = params;
        tools.push_back(tool);
    }
    
    // memory_search
    {
        Json tool;
        tool["name"] = "memory_search";
        tool["description"] = "Search structured memory entries in the database.";
        
        Json params;
        params["type"] = "object";
        Json props;
        { Json p; p["type"] = "string"; p["description"] = "Search query"; props["query"] = p; }
        { Json p; p["type"] = "integer"; p["description"] = "Maximum number of results (default: 10)"; props["max_results"] = p; }
        params["properties"] = props;
        Json required = Json::array();
        required.push_back("query");
        params["required"] = required;
        tool["parameters"] = params;
        tools.push_back(tool);
    }
    
    // memory_list
    {
        Json tool;
        tool["name"] = "memory_list";
        tool["description"] = "List structured memory entries from the database.";
        
        Json params;
        params["type"] = "object";
        Json props;
        { Json p; p["type"] = "string"; p["description"] = "Filter by category (empty = all)"; props["category"] = p; }
        { Json p; p["type"] = "integer"; p["description"] = "Maximum entries to return (default: 100)"; props["limit"] = p; }
        params["properties"] = props;
        tool["parameters"] = params;
        tools.push_back(tool);
    }
    
    // memory_delete
    {
        Json tool;
        tool["name"] = "memory_delete";
        tool["description"] = "Delete a structured memory entry from the database.";
        
        Json params;
        params["type"] = "object";
        Json props;
        { Json p; p["type"] = "string"; p["description"] = "The memory entry ID to delete"; props["id"] = p; }
        params["properties"] = props;
        Json required = Json::array();
        required.push_back("id");
        params["required"] = required;
        tool["parameters"] = params;
        tools.push_back(tool);
    }
    
    // ================================================================
    // TASK operations schema (DATABASE)
    // ================================================================
    
    // task_create
    {
        Json tool;
        tool["name"] = "task_create";
        tool["description"] = "Create a task or reminder.";
        
        Json params;
        params["type"] = "object";
        Json props;
        { Json p; p["type"] = "string"; p["description"] = "The task description"; props["content"] = p; }
        { Json p; p["type"] = "string"; p["description"] = "Additional context or notes"; props["context"] = p; }
        { Json p; p["type"] = "integer"; p["description"] = "Due date as Unix timestamp in ms (0 = no due date)"; props["due_at"] = p; }
        params["properties"] = props;
        Json required = Json::array();
        required.push_back("content");
        params["required"] = required;
        tool["parameters"] = params;
        tools.push_back(tool);
    }
    
    // task_complete
    {
        Json tool;
        tool["name"] = "task_complete";
        tool["description"] = "Mark a task as completed.";
        
        Json params;
        params["type"] = "object";
        Json props;
        { Json p; p["type"] = "string"; p["description"] = "The task ID to complete"; props["task_id"] = p; }
        params["properties"] = props;
        Json required = Json::array();
        required.push_back("task_id");
        params["required"] = required;
        tool["parameters"] = params;
        tools.push_back(tool);
    }
    
    // task_list
    {
        Json tool;
        tool["name"] = "task_list";
        tool["description"] = "List all tasks.";
        
        Json params;
        params["type"] = "object";
        Json props;
        { Json p; p["type"] = "boolean"; p["description"] = "Whether to include completed tasks (default: false)"; props["include_completed"] = p; }
        params["properties"] = props;
        tool["parameters"] = params;
        tools.push_back(tool);
    }
    
    return tools;
}

ToolResult MemoryTool::execute(const std::string& action, const Json& params) {
    if (!manager_) {
        return ToolResult::fail("Memory tool not initialized");
    }

    Json response = execute_function(action, params);
    ToolResult result;
    result.data = response;

    bool ok = response.value("success", false);
    result.success = ok;
    if (!ok) {
        result.error = response.value("error", std::string("Unknown error"));
    }

    return result;
}

Json MemoryTool::execute_function(const std::string& function_name, const Json& params) {
    if (!manager_) {
        return make_error("Memory tool not initialized");
    }
    
    // File operations (filesystem)
    if (function_name == "file_save") {
        return file_save(params);
    } else if (function_name == "file_get") {
        return file_get(params);
    } else if (function_name == "file_list") {
        return file_list(params);
    }
    // Memory operations (database)
    else if (function_name == "memory_save") {
        return memory_save(params);
    } else if (function_name == "memory_search") {
        return memory_search(params);
    } else if (function_name == "memory_list") {
        return memory_list(params);
    } else if (function_name == "memory_delete") {
        return memory_delete(params);
    }
    // Task operations (database)
    else if (function_name == "task_create") {
        return task_create(params);
    } else if (function_name == "task_complete") {
        return task_complete(params);
    } else if (function_name == "task_list") {
        return task_list(params);
    }
    
    return make_error("Unknown function: " + function_name);
}

MemoryManager* MemoryTool::memory_manager() {
    return manager_.get();
}

const MemoryManager* MemoryTool::memory_manager() const {
    return manager_.get();
}

// ============================================================================
// FILE operations - filesystem only
// ============================================================================

Json MemoryTool::file_save(const Json& params) {
    std::string content = params.value("content", std::string(""));
    if (content.empty()) {
        return make_error("Content is required");
    }
    
    bool daily = params.value("daily", false);
    bool append = params.value("append", false);
    std::string filename = params.value("filename", std::string(""));
    
    bool ok;
    if (daily) {
        ok = manager_->save_daily_memory(content);
    } else if (append) {
        std::string target = filename.empty() ? "MEMORY.md" : filename;
        ok = manager_->append_to_memory(content, target);
    } else {
        ok = manager_->save_memory(content, filename);
    }
    
    if (ok) {
        return make_success("File saved successfully");
    } else {
        return make_error("Failed to save file: " + manager_->last_error());
    }
}

Json MemoryTool::file_get(const Json& params) {
    std::string path = params.value("path", std::string(""));
    if (path.empty()) {
        return make_error("Path is required");
    }
    
    std::string content = manager_->get_memory_content(path);
    
    if (content.empty()) {
        return make_error("File not found or empty: " + path);
    }
    
    Json response;
    response["success"] = true;
    response["path"] = path;
    response["content"] = content;
    return response;
}

Json MemoryTool::file_list(const Json& params) {
    (void)params;
    
    std::vector<std::string> files = manager_->list_memory_files();
    
    Json response;
    response["success"] = true;
    
    Json items = Json::array();
    for (const auto& f : files) {
        items.push_back(f);
    }
    
    response["files"] = items;
    response["count"] = static_cast<int>(files.size());
    return response;
}

// ============================================================================
// MEMORY operations - DATABASE only (structured entries in SQLite)
// ============================================================================

Json MemoryTool::memory_save(const Json& params) {
    std::string content = params.value("content", std::string(""));
    if (content.empty()) {
        return make_error("Content is required");
    }
    
    std::string category = params.value("category", std::string("general"));
    int importance = params.value("importance", 5);
    std::string tags = params.value("tags", std::string(""));
    
    bool ok = manager_->save_structured_memory(content, category, tags, importance);
    
    if (ok) {
        return make_success("Memory saved to database");
    } else {
        return make_error("Failed to save memory: " + manager_->last_error());
    }
}

Json MemoryTool::memory_search(const Json& params) {
    std::string query = params.value("query", std::string(""));
    if (query.empty()) {
        return make_error("Query is required");
    }
    
    int max_results = params.value("max_results", 10);
    
    // DATABASE only - search structured memories
    std::vector<MemoryEntry> memories = manager_->search_structured_memories(query, max_results);
    
    Json response;
    response["success"] = true;
    
    Json items = Json::array();
    for (const auto& m : memories) {
        Json item;
        item["id"] = m.id;
        item["content"] = m.content;
        item["category"] = m.category;
        item["tags"] = m.tags;
        item["importance"] = m.importance;
        items.push_back(item);
    }
    
    response["memories"] = items;
    response["count"] = static_cast<int>(memories.size());
    return response;
}

Json MemoryTool::memory_list(const Json& params) {
    std::string category = params.value("category", std::string(""));
    int limit = params.value("limit", 100);
    
    // DATABASE only - list structured memory entries
    std::vector<MemoryEntry> memories = manager_->list_structured_memories(category, limit);
    
    Json response;
    response["success"] = true;
    
    Json items = Json::array();
    for (const auto& m : memories) {
        Json item;
        item["id"] = m.id;
        item["content"] = m.content;
        item["category"] = m.category;
        item["tags"] = m.tags;
        item["importance"] = m.importance;
        items.push_back(item);
    }
    
    response["memories"] = items;
    response["count"] = static_cast<int>(memories.size());
    return response;
}

Json MemoryTool::memory_delete(const Json& params) {
    std::string id = params.value("id", std::string(""));
    if (id.empty()) {
        return make_error("Memory ID is required");
    }
    
    if (manager_->delete_structured_memory(id)) {
        return make_success("Memory deleted from database");
    } else {
        return make_error("Failed to delete memory");
    }
}

// ============================================================================
// TASK operations - DATABASE only
// ============================================================================

Json MemoryTool::task_create(const Json& params) {
    std::string content = params.value("content", std::string(""));
    if (content.empty()) {
        return make_error("Content is required");
    }
    
    std::string context = params.value("context", std::string(""));
    std::string task_id = manager_->create_task(content, context);
    
    if (task_id.empty()) {
        return make_error("Failed to create task");
    }
    
    int64_t due_at = params.value("due_at", int64_t(0));
    if (due_at > 0) {
        manager_->update_task_due(task_id, due_at);
    }
    
    Json response;
    response["success"] = true;
    response["task_id"] = task_id;
    response["message"] = "Task created successfully";
    return response;
}

Json MemoryTool::task_complete(const Json& params) {
    std::string task_id = params.value("task_id", std::string(""));
    if (task_id.empty()) {
        return make_error("Task ID is required");
    }
    
    if (manager_->complete_task(task_id)) {
        return make_success("Task completed successfully");
    } else {
        return make_error("Failed to complete task");
    }
}

Json MemoryTool::task_list(const Json& params) {
    bool include_completed = params.value("include_completed", false);
    
    std::vector<MemoryTask> tasks = manager_->list_tasks(include_completed);
    
    Json response;
    response["success"] = true;
    
    Json items = Json::array();
    for (const auto& t : tasks) {
        Json item;
        item["id"] = t.id;
        item["content"] = t.content;
        item["context"] = t.context;
        item["created_at"] = static_cast<int>(t.created_at);
        item["due_at"] = static_cast<int>(t.due_at);
        item["completed"] = t.completed;
        if (t.completed) {
            item["completed_at"] = static_cast<int>(t.completed_at);
        }
        items.push_back(item);
    }
    
    response["tasks"] = items;
    response["count"] = static_cast<int>(tasks.size());
    return response;
}

// ============================================================================
// Helpers
// ============================================================================

Json MemoryTool::make_error(const std::string& message) {
    Json response;
    response["success"] = false;
    response["error"] = message;
    return response;
}

Json MemoryTool::make_success(const std::string& message) {
    Json response;
    response["success"] = true;
    response["message"] = message;
    return response;
}

} // namespace opencrank
