/*
 * opencrank C++11 - Memory SQLite Store
 * 
 * SQLite-based storage with clean separation of concerns:
 *   FileStore        - File tracking and chunk indexing (filesystem metadata in DB)
 *   MemoryEntryStore - Structured memory entries (database-only)
 *   TaskStore        - Task/reminder management (database-only)
 *   MemoryStore      - Owns the sqlite3 handle, composes the above
 */
#ifndef opencrank_MEMORY_STORE_HPP
#define opencrank_MEMORY_STORE_HPP

#include "types.hpp"
#include <string>
#include <vector>
#include <sqlite3.h>

namespace opencrank {

// A structured memory entry stored in SQLite
struct MemoryEntry {
    std::string id;             // Unique memory ID
    std::string content;        // The memory content
    std::string category;       // Category: "general", "resume", "fact", "preference", etc.
    std::string session_key;    // Session that created this memory (optional)
    std::string tags;           // Comma-separated tags for search
    int64_t created_at;         // Creation timestamp (unix ms)
    int64_t updated_at;         // Last update timestamp (unix ms)
    int64_t expires_at;         // Expiration timestamp (0 = never)
    int importance;             // 1-10 importance score
    
    MemoryEntry() : created_at(0), updated_at(0), expires_at(0), importance(5) {}
};

// ============================================================================
// FileStore - File tracking + chunk indexing + FTS search
//
// Manages the 'files', 'chunks', and 'chunks_fts' tables.
// This is the bridge between filesystem content and searchable DB indexes.
// ============================================================================
class FileStore {
public:
    explicit FileStore(sqlite3*& db);
    
    // Schema
    bool ensure_schema();
    
    // File tracking (files table)
    bool upsert_file(const MemoryFile& file);
    bool delete_file(const std::string& path, MemorySource source);
    bool get_file(const std::string& path, MemorySource source, MemoryFile& out);
    std::vector<MemoryFile> list_files(MemorySource source);
    std::vector<std::string> get_stale_paths(const std::vector<std::string>& active_paths, MemorySource source);
    
    // Chunk indexing (chunks table + FTS)
    bool upsert_chunk(const MemoryChunk& chunk);
    bool delete_chunks_for_file(const std::string& path, MemorySource source);
    std::vector<MemoryChunk> get_chunks_for_file(const std::string& path, MemorySource source);
    int count_chunks(MemorySource source);
    
    // Search (BM25 full-text search over chunks)
    std::vector<MemorySearchResult> search(const std::string& query, const MemorySearchConfig& config);
    
    std::string last_error() const;
    
private:
    sqlite3*& db_;
    std::string last_error_;
    bool fts_available_;
    
    bool ensure_fts_table();
    bool sync_chunk_to_fts(const MemoryChunk& chunk);
    bool delete_chunk_from_fts(const std::string& chunk_id);
    
    bool exec(const std::string& sql);
    bool exec(const std::string& sql, std::string& error);
    void set_error(const std::string& error);
    void set_error_from_db();
};

// ============================================================================
// MemoryEntryStore - Structured memory entries (DATABASE ONLY)
//
// Manages the 'memories' and 'memories_fts' tables.
// CRUD + FTS search for persistent knowledge entries.
// ============================================================================
class MemoryEntryStore {
public:
    explicit MemoryEntryStore(sqlite3*& db);
    
    // Schema
    bool ensure_schema();
    
    // CRUD
    bool upsert(const MemoryEntry& entry);
    bool remove(const std::string& id);
    bool get(const std::string& id, MemoryEntry& out);
    std::vector<MemoryEntry> list(const std::string& category = "", int limit = 100);
    int count(const std::string& category = "");
    
    // Search (FTS + fallback LIKE)
    std::vector<MemoryEntry> search(const std::string& query, int limit = 10);
    
    // Maintenance
    bool cleanup_expired();
    
    std::string last_error() const;
    
private:
    sqlite3*& db_;
    std::string last_error_;
    
    bool exec(const std::string& sql);
    bool exec(const std::string& sql, std::string& error);
    void set_error(const std::string& error);
    void set_error_from_db();
};

// ============================================================================
// TaskStore - Task/reminder management (DATABASE ONLY)
//
// Manages the 'tasks' table.
// ============================================================================
class TaskStore {
public:
    explicit TaskStore(sqlite3*& db);
    
    // Schema
    bool ensure_schema();
    
    // CRUD
    bool upsert(const MemoryTask& task);
    bool remove(const std::string& id);
    bool get(const std::string& id, MemoryTask& out);
    std::vector<MemoryTask> list(bool include_completed = false);
    
    // Queries
    std::vector<MemoryTask> get_pending();
    std::vector<MemoryTask> get_due_before(int64_t timestamp);
    bool complete(const std::string& id);
    
    std::string last_error() const;
    
private:
    sqlite3*& db_;
    std::string last_error_;
    
    bool exec(const std::string& sql);
    void set_error(const std::string& error);
    void set_error_from_db();
};

// ============================================================================
// MemoryStore - Top-level store, owns the sqlite3 handle
//
// Composes FileStore, MemoryEntryStore, and TaskStore.
// Provides delegated methods for backward compatibility.
// ============================================================================
class MemoryStore {
public:
    MemoryStore();
    ~MemoryStore();
    
    // Database lifecycle
    bool open(const std::string& db_path);
    void close();
    bool is_open() const;
    
    // Schema management (initializes all sub-stores)
    bool ensure_schema();
    
    // Sub-store access
    FileStore& files() { return files_; }
    MemoryEntryStore& memories() { return memories_; }
    TaskStore& tasks() { return tasks_; }
    
    // ---- Delegated File operations ----
    bool upsert_file(const MemoryFile& file);
    bool delete_file(const std::string& path, MemorySource source);
    bool get_file(const std::string& path, MemorySource source, MemoryFile& out);
    std::vector<MemoryFile> list_files(MemorySource source);
    std::vector<std::string> get_stale_paths(const std::vector<std::string>& active_paths, MemorySource source);
    bool upsert_chunk(const MemoryChunk& chunk);
    bool delete_chunks_for_file(const std::string& path, MemorySource source);
    std::vector<MemoryChunk> get_chunks_for_file(const std::string& path, MemorySource source);
    int count_chunks(MemorySource source);
    std::vector<MemorySearchResult> search(const std::string& query, const MemorySearchConfig& config);
    
    // ---- Delegated Memory operations ----
    bool upsert_memory(const MemoryEntry& entry);
    bool delete_memory(const std::string& id);
    bool get_memory(const std::string& id, MemoryEntry& out);
    std::vector<MemoryEntry> list_memories(const std::string& category = "", int limit = 100);
    std::vector<MemoryEntry> search_memories(const std::string& query, int limit = 10);
    bool cleanup_expired_memories();
    int count_memories(const std::string& category = "");
    
    // ---- Delegated Task operations ----
    bool upsert_task(const MemoryTask& task);
    bool delete_task(const std::string& id);
    bool get_task(const std::string& id, MemoryTask& out);
    std::vector<MemoryTask> list_tasks(bool include_completed = false);
    std::vector<MemoryTask> get_pending_tasks();
    std::vector<MemoryTask> get_tasks_due_before(int64_t timestamp);
    bool complete_task(const std::string& id);
    
    // Meta operations
    bool set_meta(const std::string& key, const std::string& value);
    std::string get_meta(const std::string& key, const std::string& default_val = "");
    
    // Utility
    std::string last_error() const;
    
private:
    sqlite3* db_;
    std::string last_error_;
    
    FileStore files_;
    MemoryEntryStore memories_;
    TaskStore tasks_;
    
    bool exec(const std::string& sql);
    bool exec(const std::string& sql, std::string& error);
    void set_error(const std::string& error);
    void set_error_from_db();
};

} // namespace opencrank

#endif // opencrank_MEMORY_STORE_HPP
