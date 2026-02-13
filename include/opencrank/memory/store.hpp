/*
 * opencrank C++ - Memory Store
 * 
 * SQLite storage backend for the memory system.
 * Handles persistent storage of memory entries, tasks, and provides
 * BM25 full-text search via SQLite FTS5.
 * 
 * All operations are database read/write - no file I/O.
 */
#ifndef opencrank_MEMORY_STORE_HPP
#define opencrank_MEMORY_STORE_HPP

#include "types.hpp"
#include <string>
#include <vector>
#include <cstdint>

// Forward declaration for sqlite3
struct sqlite3;
struct sqlite3_stmt;

namespace opencrank {

// A stored memory entry
struct MemoryEntry {
    std::string id;         // UUID
    std::string content;    // Memory text
    std::string category;   // Category (general, resume, note, etc.)
    std::string tags;       // Comma-separated tags
    std::string channel;    // Channel where created
    std::string user_id;    // User who created it
    int importance;         // 1-10 importance level
    int64_t created_at;     // Unix timestamp ms
    int64_t updated_at;     // Unix timestamp ms
    
    MemoryEntry() : importance(5), created_at(0), updated_at(0) {}
};

// Result from a memory database search
struct MemorySearchHit {
    MemoryEntry entry;
    double score;           // BM25 relevance score
    std::string snippet;    // FTS5 snippet with highlights
    
    MemorySearchHit() : score(0.0) {}
};

// ============================================================================
// MemoryStore - SQLite Storage Backend
// ============================================================================

class MemoryStore {
public:
    MemoryStore();
    ~MemoryStore();
    
    // Open/close the database
    bool open(const std::string& db_path);
    void close();
    bool is_open() const { return db_ != nullptr; }
    
    // ========================================================================
    // Memory Operations (database read/write)
    // ========================================================================
    
    // Save a memory entry (insert or update by id)
    bool save_memory(const MemoryEntry& entry);
    
    // Search memories using BM25 full-text search
    std::vector<MemorySearchHit> search_memories(
        const std::string& query, 
        int max_results = 10,
        const std::string& category_filter = ""
    );
    
    // Get a specific memory by ID
    MemoryEntry get_memory(const std::string& id);
    
    // Get recent memories (ordered by updated_at desc)
    std::vector<MemoryEntry> get_recent_memories(
        int limit = 10,
        const std::string& category_filter = ""
    );
    
    // Delete a memory by ID
    bool delete_memory(const std::string& id);
    
    // ========================================================================
    // Task Operations (database read/write)
    // ========================================================================
    
    // Create a new task
    bool create_task(const MemoryTask& task);
    
    // List tasks (optionally filter by completed status)
    std::vector<MemoryTask> list_tasks(
        bool include_completed = false,
        const std::string& channel_filter = ""
    );
    
    // Get a specific task by ID
    MemoryTask get_task(const std::string& id);
    
    // Mark a task as completed
    bool complete_task(const std::string& id);
    
    // Delete a task by ID
    bool delete_task(const std::string& id);
    
    // Get tasks that are due (due_at <= now and not completed)
    std::vector<MemoryTask> get_due_tasks();

private:
    sqlite3* db_;
    
    // Initialize database tables and FTS index
    bool init_tables();
    
    // Helper: execute a simple SQL statement
    bool exec_sql(const std::string& sql);
    
    // Helper: generate a UUID
    static std::string generate_uuid();
    
    // Helper: current time in milliseconds
    static int64_t now_ms();
    
    // Helper: bind and step a prepared statement
    bool bind_and_step(sqlite3_stmt* stmt);
};

} // namespace opencrank

#endif // opencrank_MEMORY_STORE_HPP
