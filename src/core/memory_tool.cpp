/*
 * OpenCrank C++11 - Memory Tool (Core) Implementation
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
    acts.push_back("memory_save");
    acts.push_back("memory_search");
    acts.push_back("memory_get");
    acts.push_back("memory_list");
    acts.push_back("task_create");
    acts.push_back("task_complete");
    acts.push_back("task_list");
    return acts;
}

std::vector<AgentTool> MemoryTool::get_agent_tools() const {
    std::vector<AgentTool> tools;
    ToolProvider* self = const_cast<MemoryTool*>(this);
    
    // memory_save - Save content to long-term memory
    {
        AgentTool tool;
        tool.name = "memory_save";
        tool.description = "Save important content to long-term memory. Use for facts, preferences, or context that should persist across sessions.";
        tool.params.push_back(ToolParamSchema("content", "string", "The content to save to memory", true));
        tool.params.push_back(ToolParamSchema("filename", "string", "Optional filename (default: MEMORY.md)", false));
        tool.params.push_back(ToolParamSchema("daily", "boolean", "If true, save to daily memory file (memory/YYYY-MM-DD.md)", false));
        tool.params.push_back(ToolParamSchema("append", "boolean", "If true, append to existing file instead of overwriting", false));
        
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
    
    // memory_search - Search through memories
    {
        AgentTool tool;
        tool.name = "memory_search";
        tool.description = "Search through memories for relevant information. Returns snippets ranked by relevance.";
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
    
    // memory_get - Get full content of a memory file
    {
        AgentTool tool;
        tool.name = "memory_get";
        tool.description = "Get the full content of a specific memory file.";
        tool.params.push_back(ToolParamSchema("path", "string", "Path to the memory file (e.g., 'MEMORY.md' or 'memory/2024-01-15.md')", true));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult result = self->execute("memory_get", params);
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
    
    // memory_list - List all memory files
    {
        AgentTool tool;
        tool.name = "memory_list";
        tool.description = "List all memory files in the workspace.";
        // No required parameters
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult result = self->execute("memory_list", params);
            if (!result.success) {
                return AgentToolResult::fail(result.error);
            }
            return AgentToolResult::ok(result.data.dump(2));
        };
        
        tools.push_back(tool);
    }
    
    // task_create - Create a task or reminder
    {
        AgentTool tool;
        tool.name = "task_create";
        tool.description = "Create a task or reminder for later. Tasks persist across sessions.";
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
        tool.description = "List all tasks. Shows pending tasks by default.";
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
        return true;  // Already initialized
    }
    
    // Extract memory configuration
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
    
    // Perform initial sync
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
    
    // memory_save tool
    {
        Json tool;
        tool["name"] = "memory_save";
        tool["description"] = "Save content to long-term memory. Use for important facts, preferences, or context that should persist across sessions.";
        
        Json params;
        params["type"] = "object";
        
        Json props;
        {
            Json content;
            content["type"] = "string";
            content["description"] = "The content to save to memory";
            props["content"] = content;
        }
        {
            Json filename;
            filename["type"] = "string";
            filename["description"] = "Optional filename (default: MEMORY.md or memory/YYYY-MM-DD.md for daily)";
            props["filename"] = filename;
        }
        {
            Json daily;
            daily["type"] = "boolean";
            daily["description"] = "If true, save to daily memory file (memory/YYYY-MM-DD.md)";
            props["daily"] = daily;
        }
        {
            Json append;
            append["type"] = "boolean";
            append["description"] = "If true, append to existing file instead of overwriting";
            props["append"] = append;
        }
        
        params["properties"] = props;
        
        Json required = Json::array();
        required.push_back("content");
        params["required"] = required;
        
        tool["parameters"] = params;
        tools.push_back(tool);
    }
    
    // memory_search tool
    {
        Json tool;
        tool["name"] = "memory_search";
        tool["description"] = "Search through memories for relevant information. Returns snippets ranked by relevance.";
        
        Json params;
        params["type"] = "object";
        
        Json props;
        {
            Json query;
            query["type"] = "string";
            query["description"] = "Search query - keywords or natural language";
            props["query"] = query;
        }
        {
            Json max_results;
            max_results["type"] = "integer";
            max_results["description"] = "Maximum number of results (default: 10)";
            props["max_results"] = max_results;
        }
        
        params["properties"] = props;
        
        Json required = Json::array();
        required.push_back("query");
        params["required"] = required;
        
        tool["parameters"] = params;
        tools.push_back(tool);
    }
    
    // memory_get tool
    {
        Json tool;
        tool["name"] = "memory_get";
        tool["description"] = "Get the full content of a specific memory file.";
        
        Json params;
        params["type"] = "object";
        
        Json props;
        {
            Json path;
            path["type"] = "string";
            path["description"] = "Path to the memory file (e.g., 'MEMORY.md' or 'memory/2024-01-15.md')";
            props["path"] = path;
        }
        
        params["properties"] = props;
        
        Json required = Json::array();
        required.push_back("path");
        params["required"] = required;
        
        tool["parameters"] = params;
        tools.push_back(tool);
    }
    
    // memory_list tool
    {
        Json tool;
        tool["name"] = "memory_list";
        tool["description"] = "List all memory files.";
        
        Json params;
        params["type"] = "object";
        params["properties"] = Json::object();  // No properties
        
        tool["parameters"] = params;
        tools.push_back(tool);
    }
    
    // task_create tool
    {
        Json tool;
        tool["name"] = "task_create";
        tool["description"] = "Create a task or reminder for later.";
        
        Json params;
        params["type"] = "object";
        
        Json props;
        {
            Json content;
            content["type"] = "string";
            content["description"] = "The task description";
            props["content"] = content;
        }
        {
            Json context;
            context["type"] = "string";
            context["description"] = "Additional context or notes";
            props["context"] = context;
        }
        {
            Json due_at;
            due_at["type"] = "integer";
            due_at["description"] = "Due date as Unix timestamp in milliseconds (0 = no due date)";
            props["due_at"] = due_at;
        }
        
        params["properties"] = props;
        
        Json required = Json::array();
        required.push_back("content");
        params["required"] = required;
        
        tool["parameters"] = params;
        tools.push_back(tool);
    }
    
    // task_complete tool
    {
        Json tool;
        tool["name"] = "task_complete";
        tool["description"] = "Mark a task as completed.";
        
        Json params;
        params["type"] = "object";
        
        Json props;
        {
            Json task_id;
            task_id["type"] = "string";
            task_id["description"] = "The task ID to complete";
            props["task_id"] = task_id;
        }
        
        params["properties"] = props;
        
        Json required = Json::array();
        required.push_back("task_id");
        params["required"] = required;
        
        tool["parameters"] = params;
        tools.push_back(tool);
    }
    
    // task_list tool
    {
        Json tool;
        tool["name"] = "task_list";
        tool["description"] = "List all tasks.";
        
        Json params;
        params["type"] = "object";
        
        Json props;
        {
            Json include_completed;
            include_completed["type"] = "boolean";
            include_completed["description"] = "Whether to include completed tasks (default: false)";
            props["include_completed"] = include_completed;
        }
        
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
    
    if (function_name == "memory_save") {
        return memory_save(params);
    } else if (function_name == "memory_search") {
        return memory_search(params);
    } else if (function_name == "memory_get") {
        return memory_get(params);
    } else if (function_name == "memory_list") {
        return memory_list(params);
    } else if (function_name == "task_create") {
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

Json MemoryTool::memory_save(const Json& params) {
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
        return make_success("Memory saved successfully");
    } else {
        return make_error("Failed to save memory: " + manager_->last_error());
    }
}

Json MemoryTool::memory_search(const Json& params) {
    std::string query = params.value("query", std::string(""));
    if (query.empty()) {
        return make_error("Query is required");
    }
    
    MemorySearchConfig config;
    config.max_results = params.value("max_results", 10);
    
    std::vector<MemorySearchResult> results = manager_->search(query, config);
    
    Json response;
    response["success"] = true;
    
    Json items = Json::array();
    
    for (const auto& r : results) {
        Json item;
        item["path"] = r.path;
        item["score"] = r.score;
        item["snippet"] = r.snippet;
        item["start_line"] = r.start_line;
        item["end_line"] = r.end_line;
        item["source"] = memory_source_to_string(r.source);
        items.push_back(item);
    }
    
    response["results"] = items;
    response["count"] = static_cast<int>(results.size());
    
    return response;
}

Json MemoryTool::memory_get(const Json& params) {
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

Json MemoryTool::memory_list(const Json& params) {
    (void)params;  // Unused
    
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
    
    // Update due date if provided
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
