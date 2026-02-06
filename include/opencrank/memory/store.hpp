/*
 * opencrank C++11 - Memory SQLite Store
 * 
 * SQLite-based storage for memory indexing and search.
 * Uses FTS5 for full-text search (BM25 ranking).
 */
#ifndef opencrank_MEMORY_STORE_HPP
#define opencrank_MEMORY_STORE_HPP

#include "types.hpp"
#include <string>
#include <vector>
#include <sqlite3.h>

namespace opencrank {

class MemoryStore {
public:
    MemoryStore();
    ~MemoryStore();
    
    // Initialize database at path
    bool open(const std::string& db_path);
    void close();
    bool is_open() const;
    
    // Schema management
    bool ensure_schema();
    
    // File operations
    bool upsert_file(const MemoryFile& file);
    bool delete_file(const std::string& path, MemorySource source);
    bool get_file(const std::string& path, MemorySource source, MemoryFile& out);
    std::vector<MemoryFile> list_files(MemorySource source);
    std::vector<std::string> get_stale_paths(const std::vector<std::string>& active_paths, MemorySource source);
    
    // Chunk operations
    bool upsert_chunk(const MemoryChunk& chunk);
    bool delete_chunks_for_file(const std::string& path, MemorySource source);
    std::vector<MemoryChunk> get_chunks_for_file(const std::string& path, MemorySource source);
    int count_chunks(MemorySource source);
    
    // Search operations (BM25 full-text search)
    std::vector<MemorySearchResult> search(const std::string& query, const MemorySearchConfig& config);
    
    // Task operations
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
    bool fts_available_;
    
    bool exec(const std::string& sql);
    bool exec(const std::string& sql, std::string& error);
    void set_error(const std::string& error);
    void set_error_from_db();
    
    // FTS helpers
    bool ensure_fts_table();
    bool sync_chunk_to_fts(const MemoryChunk& chunk);
    bool delete_chunk_from_fts(const std::string& chunk_id);
};

} // namespace opencrank

#endif // opencrank_MEMORY_STORE_HPP
