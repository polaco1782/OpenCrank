/*
 * OpenCrank C++11 - Memory SQLite Store Implementation
 * 
 * Clean separation:
 *   FileStore        - File tracking + chunk indexing + FTS search
 *   MemoryEntryStore - Structured memory entries (DB only)
 *   TaskStore        - Task/reminder management (DB only)
 *   MemoryStore      - Owns sqlite3 handle, composes the above
 */
#include <opencrank/memory/store.hpp>
#include <opencrank/core/logger.hpp>
#include <cstring>
#include <ctime>
#include <sstream>

namespace opencrank {

// ============================================================================
// FileStore implementation
// ============================================================================

FileStore::FileStore(sqlite3*& db)
    : db_(db)
    , fts_available_(false)
{
}

bool FileStore::ensure_schema() {
    if (!db_) {
        set_error("Database not open");
        return false;
    }
    
    // Files table for tracking indexed files
    if (!exec(
        "CREATE TABLE IF NOT EXISTS files ("
        "  path TEXT NOT NULL,"
        "  source TEXT NOT NULL,"
        "  abs_path TEXT,"
        "  hash TEXT,"
        "  mtime INTEGER,"
        "  size INTEGER,"
        "  PRIMARY KEY (path, source)"
        ")"
    )) return false;
    
    // Chunks table for text chunks
    if (!exec(
        "CREATE TABLE IF NOT EXISTS chunks ("
        "  id TEXT PRIMARY KEY,"
        "  path TEXT NOT NULL,"
        "  source TEXT NOT NULL,"
        "  start_line INTEGER,"
        "  end_line INTEGER,"
        "  text TEXT,"
        "  hash TEXT,"
        "  updated_at INTEGER"
        ")"
    )) return false;
    
    if (!exec("CREATE INDEX IF NOT EXISTS idx_chunks_path ON chunks(path, source)")) return false;
    
    // Try to create FTS5 table for full-text search
    fts_available_ = ensure_fts_table();
    
    return true;
}

bool FileStore::ensure_fts_table() {
    std::string error;
    bool ok = exec(
        "CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5("
        "  chunk_id,"
        "  path,"
        "  source,"
        "  text,"
        "  content=chunks,"
        "  content_rowid=rowid"
        ")",
        error
    );
    
    if (!ok) {
        // FTS5 might not be available, try FTS4
        ok = exec(
            "CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts4("
            "  chunk_id,"
            "  path,"
            "  source,"
            "  text"
            ")",
            error
        );
    }
    
    return ok;
}

// ---- File tracking ----

bool FileStore::upsert_file(const MemoryFile& file) {
    if (!db_) {
        set_error("Database not open");
        return false;
    }
    
    LOG_DEBUG("[FileStore] Upserting file: path=%s, source=%s, hash=%s",
              file.path.c_str(), memory_source_to_string(file.source).c_str(), file.hash.c_str());
    
    const char* sql = 
        "INSERT OR REPLACE INTO files (path, source, abs_path, hash, mtime, size) "
        "VALUES (?, ?, ?, ?, ?, ?)";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        set_error_from_db();
        return false;
    }
    
    std::string source = memory_source_to_string(file.source);
    sqlite3_bind_text(stmt, 1, file.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, file.abs_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, file.hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, file.mtime);
    sqlite3_bind_int64(stmt, 6, file.size);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        set_error_from_db();
        return false;
    }
    
    return true;
}

bool FileStore::delete_file(const std::string& path, MemorySource source) {
    if (!db_) {
        set_error("Database not open");
        return false;
    }
    
    LOG_DEBUG("[FileStore] Deleting file: path=%s, source=%s",
              path.c_str(), memory_source_to_string(source).c_str());
    
    std::string src = memory_source_to_string(source);
    
    const char* sql = "DELETE FROM files WHERE path = ? AND source = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        set_error_from_db();
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, src.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool FileStore::get_file(const std::string& path, MemorySource source, MemoryFile& out) {
    if (!db_) {
        set_error("Database not open");
        return false;
    }
    
    LOG_DEBUG("[FileStore] Getting file: path=%s, source=%s",
              path.c_str(), memory_source_to_string(source).c_str());
    
    std::string src = memory_source_to_string(source);
    
    const char* sql = "SELECT path, source, abs_path, hash, mtime, size FROM files WHERE path = ? AND source = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        set_error_from_db();
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, src.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        out.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        out.source = string_to_memory_source(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))
        );
        out.abs_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        out.hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        out.mtime = sqlite3_column_int64(stmt, 4);
        out.size = sqlite3_column_int64(stmt, 5);
        sqlite3_finalize(stmt);
        return true;
    }
    
    sqlite3_finalize(stmt);
    return false;
}

std::vector<MemoryFile> FileStore::list_files(MemorySource source) {
    std::vector<MemoryFile> result;
    if (!db_) return result;
    
    LOG_DEBUG("[FileStore] Listing files: source=%s", memory_source_to_string(source).c_str());
    
    std::string src = memory_source_to_string(source);
    
    const char* sql = "SELECT path, source, abs_path, hash, mtime, size FROM files WHERE source = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;
    
    sqlite3_bind_text(stmt, 1, src.c_str(), -1, SQLITE_TRANSIENT);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MemoryFile f;
        f.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        f.source = string_to_memory_source(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))
        );
        const char* abs = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        f.abs_path = abs ? abs : "";
        const char* hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        f.hash = hash ? hash : "";
        f.mtime = sqlite3_column_int64(stmt, 4);
        f.size = sqlite3_column_int64(stmt, 5);
        result.push_back(f);
    }
    
    sqlite3_finalize(stmt);
    LOG_DEBUG("[FileStore] Listed %zu files", result.size());
    return result;
}

std::vector<std::string> FileStore::get_stale_paths(const std::vector<std::string>& active_paths, 
                                                     MemorySource source) {
    std::vector<std::string> stale;
    std::vector<MemoryFile> files = list_files(source);
    
    LOG_DEBUG("[FileStore] Checking for stale paths: %zu active, %zu tracked, source=%s",
              active_paths.size(), files.size(), memory_source_to_string(source).c_str());
    
    for (const auto& f : files) {
        bool found = false;
        for (const auto& p : active_paths) {
            if (p == f.path) {
                found = true;
                break;
            }
        }
        if (!found) {
            stale.push_back(f.path);
        }
    }
    
    LOG_DEBUG("[FileStore] Found %zu stale paths", stale.size());
    return stale;
}

// ---- Chunk indexing ----

bool FileStore::upsert_chunk(const MemoryChunk& chunk) {
    if (!db_) {
        set_error("Database not open");
        return false;
    }
    
    LOG_DEBUG("[FileStore] Upserting chunk: id=%s, path=%s, lines=%d-%d",
              chunk.id.c_str(), chunk.path.c_str(), chunk.start_line, chunk.end_line);
    
    const char* sql = 
        "INSERT OR REPLACE INTO chunks (id, path, source, start_line, end_line, text, hash, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        set_error_from_db();
        return false;
    }
    
    std::string source = memory_source_to_string(chunk.source);
    sqlite3_bind_text(stmt, 1, chunk.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, chunk.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, chunk.start_line);
    sqlite3_bind_int(stmt, 5, chunk.end_line);
    sqlite3_bind_text(stmt, 6, chunk.text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, chunk.hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 8, chunk.updated_at);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        set_error_from_db();
        return false;
    }
    
    // Sync to FTS if available
    if (fts_available_) {
        sync_chunk_to_fts(chunk);
    }
    
    return true;
}

bool FileStore::sync_chunk_to_fts(const MemoryChunk& chunk) {
    // Delete any existing entry first
    delete_chunk_from_fts(chunk.id);
    
    const char* sql = "INSERT INTO chunks_fts (chunk_id, path, source, text) VALUES (?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    std::string source = memory_source_to_string(chunk.source);
    sqlite3_bind_text(stmt, 1, chunk.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, chunk.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, chunk.text.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool FileStore::delete_chunk_from_fts(const std::string& chunk_id) {
    const char* sql = "DELETE FROM chunks_fts WHERE chunk_id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, chunk_id.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool FileStore::delete_chunks_for_file(const std::string& path, MemorySource source) {
    if (!db_) {
        set_error("Database not open");
        return false;
    }
    
    LOG_DEBUG("[FileStore] Deleting chunks for file: path=%s, source=%s",
              path.c_str(), memory_source_to_string(source).c_str());
    
    std::string src = memory_source_to_string(source);
    
    // Delete from FTS first if available
    if (fts_available_) {
        const char* fts_sql = "DELETE FROM chunks_fts WHERE path = ? AND source = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, fts_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, src.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    
    const char* sql = "DELETE FROM chunks WHERE path = ? AND source = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        set_error_from_db();
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, src.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

std::vector<MemoryChunk> FileStore::get_chunks_for_file(const std::string& path, MemorySource source) {
    std::vector<MemoryChunk> result;
    if (!db_) return result;
    
    LOG_DEBUG("[FileStore] Getting chunks for file: path=%s, source=%s",
              path.c_str(), memory_source_to_string(source).c_str());
    
    std::string src = memory_source_to_string(source);
    
    const char* sql = "SELECT id, path, source, start_line, end_line, text, hash, updated_at "
                      "FROM chunks WHERE path = ? AND source = ? ORDER BY start_line";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, src.c_str(), -1, SQLITE_TRANSIENT);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MemoryChunk c;
        c.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        c.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        c.source = string_to_memory_source(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))
        );
        c.start_line = sqlite3_column_int(stmt, 3);
        c.end_line = sqlite3_column_int(stmt, 4);
        const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        c.text = txt ? txt : "";
        const char* hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        c.hash = hash ? hash : "";
        c.updated_at = sqlite3_column_int64(stmt, 7);
        result.push_back(c);
    }
    
    sqlite3_finalize(stmt);
    LOG_DEBUG("[FileStore] Got %zu chunks for %s", result.size(), path.c_str());
    return result;
}

int FileStore::count_chunks(MemorySource source) {
    if (!db_) return 0;
    
    std::string src = memory_source_to_string(source);
    
    const char* sql = "SELECT COUNT(*) FROM chunks WHERE source = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    
    sqlite3_bind_text(stmt, 1, src.c_str(), -1, SQLITE_TRANSIENT);
    
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return count;
}

// ---- Search ----

std::vector<MemorySearchResult> FileStore::search(const std::string& query, 
                                                   const MemorySearchConfig& config) {
    std::vector<MemorySearchResult> results;
    if (!db_ || query.empty()) return results;
    
    LOG_DEBUG("[FileStore] Searching chunks: query='%s', max_results=%d", query.c_str(), config.max_results);
    
    // Use FTS if available
    if (fts_available_) {
        const char* sql = 
            "SELECT chunk_id, path, source, text, bm25(chunks_fts) as score "
            "FROM chunks_fts "
            "WHERE chunks_fts MATCH ? "
            "ORDER BY score "
            "LIMIT ?";
        
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, config.max_results);
            
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                MemorySearchResult r;
                r.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                r.source = string_to_memory_source(
                    reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))
                );
                const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                r.snippet = txt ? txt : "";
                double raw_score = sqlite3_column_double(stmt, 4);
                r.score = 1.0 / (1.0 - raw_score);
                r.start_line = 0;
                r.end_line = 0;
                
                if (r.score >= config.min_score) {
                    results.push_back(r);
                }
            }
            
            sqlite3_finalize(stmt);
            LOG_DEBUG("[FileStore] FTS search found %zu results", results.size());
            return results;
        }
    }
    
    // Fallback: simple LIKE search
    const char* sql = 
        "SELECT id, path, source, start_line, end_line, text FROM chunks "
        "WHERE text LIKE ? "
        "LIMIT ?";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;
    
    std::string like_query = "%" + query + "%";
    sqlite3_bind_text(stmt, 1, like_query.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, config.max_results);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MemorySearchResult r;
        r.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        r.source = string_to_memory_source(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))
        );
        r.start_line = sqlite3_column_int(stmt, 3);
        r.end_line = sqlite3_column_int(stmt, 4);
        const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        r.snippet = txt ? txt : "";
        r.score = 0.5;
        results.push_back(r);
    }
    
    sqlite3_finalize(stmt);
    LOG_DEBUG("[FileStore] LIKE search found %zu results", results.size());
    return results;
}

// ---- FileStore helpers ----

std::string FileStore::last_error() const {
    return last_error_;
}

bool FileStore::exec(const std::string& sql) {
    std::string error;
    return exec(sql, error);
}

bool FileStore::exec(const std::string& sql, std::string& error) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg) {
            error = err_msg;
            last_error_ = err_msg;
            sqlite3_free(err_msg);
        }
        return false;
    }
    return true;
}

void FileStore::set_error(const std::string& error) {
    last_error_ = error;
}

void FileStore::set_error_from_db() {
    if (db_) {
        last_error_ = sqlite3_errmsg(db_);
    }
}


// ============================================================================
// MemoryEntryStore implementation - DATABASE ONLY
// ============================================================================

MemoryEntryStore::MemoryEntryStore(sqlite3*& db)
    : db_(db)
{
}

bool MemoryEntryStore::ensure_schema() {
    if (!db_) {
        set_error("Database not open");
        return false;
    }
    
    // Memories table - structured memory storage
    if (!exec(
        "CREATE TABLE IF NOT EXISTS memories ("
        "  id TEXT PRIMARY KEY,"
        "  content TEXT NOT NULL,"
        "  category TEXT DEFAULT 'general',"
        "  session_key TEXT,"
        "  tags TEXT,"
        "  created_at INTEGER,"
        "  updated_at INTEGER,"
        "  expires_at INTEGER DEFAULT 0,"
        "  importance INTEGER DEFAULT 5"
        ")"
    )) return false;
    
    if (!exec("CREATE INDEX IF NOT EXISTS idx_memories_category ON memories(category)")) return false;
    if (!exec("CREATE INDEX IF NOT EXISTS idx_memories_importance ON memories(importance DESC)")) return false;
    if (!exec("CREATE INDEX IF NOT EXISTS idx_memories_created ON memories(created_at DESC)")) return false;
    
    // FTS for memories
    {
        std::string fts_error;
        if (!exec(
            "CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts USING fts5("
            "  memory_id,"
            "  content,"
            "  category,"
            "  tags"
            ")", fts_error)) {
            // Try FTS4 fallback
            exec(
                "CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts USING fts4("
                "  memory_id,"
                "  content,"
                "  category,"
                "  tags"
                ")", fts_error);
        }
    }
    
    return true;
}

bool MemoryEntryStore::upsert(const MemoryEntry& entry) {
    if (!db_) {
        set_error("Database not open");
        return false;
    }
    
    LOG_DEBUG("[MemoryEntryStore] Upserting: id=%s, category=%s, importance=%d, content_len=%zu",
              entry.id.c_str(), entry.category.c_str(), entry.importance, entry.content.size());
    
    const char* sql = 
        "INSERT OR REPLACE INTO memories (id, content, category, session_key, tags, "
        "created_at, updated_at, expires_at, importance) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        set_error_from_db();
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, entry.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, entry.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, entry.category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, entry.session_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, entry.tags.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, entry.created_at);
    sqlite3_bind_int64(stmt, 7, entry.updated_at);
    sqlite3_bind_int64(stmt, 8, entry.expires_at);
    sqlite3_bind_int(stmt, 9, entry.importance);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        set_error_from_db();
        LOG_DEBUG("[MemoryEntryStore] Failed to upsert: %s", entry.id.c_str());
        return false;
    }
    
    LOG_DEBUG("[MemoryEntryStore] Upserted: %s", entry.id.c_str());
    
    // Sync to FTS
    {
        const char* del_sql = "DELETE FROM memories_fts WHERE memory_id = ?";
        sqlite3_stmt* del_stmt = nullptr;
        if (sqlite3_prepare_v2(db_, del_sql, -1, &del_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(del_stmt, 1, entry.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(del_stmt);
            sqlite3_finalize(del_stmt);
        }
        
        const char* ins_sql = "INSERT INTO memories_fts (memory_id, content, category, tags) VALUES (?, ?, ?, ?)";
        sqlite3_stmt* ins_stmt = nullptr;
        if (sqlite3_prepare_v2(db_, ins_sql, -1, &ins_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(ins_stmt, 1, entry.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ins_stmt, 2, entry.content.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ins_stmt, 3, entry.category.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ins_stmt, 4, entry.tags.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(ins_stmt);
            sqlite3_finalize(ins_stmt);
        }
    }
    
    return true;
}

bool MemoryEntryStore::remove(const std::string& id) {
    if (!db_) return false;
    
    LOG_DEBUG("[MemoryEntryStore] Removing: %s", id.c_str());
    
    // Delete from FTS
    {
        const char* fts_sql = "DELETE FROM memories_fts WHERE memory_id = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, fts_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    
    const char* sql = "DELETE FROM memories WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    LOG_DEBUG("[MemoryEntryStore] Removed: %s (success=%s)", id.c_str(), (rc == SQLITE_DONE) ? "true" : "false");
    return rc == SQLITE_DONE;
}

bool MemoryEntryStore::get(const std::string& id, MemoryEntry& out) {
    if (!db_) return false;
    
    LOG_DEBUG("[MemoryEntryStore] Getting: %s", id.c_str());
    
    const char* sql = "SELECT id, content, category, session_key, tags, "
                      "created_at, updated_at, expires_at, importance "
                      "FROM memories WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        out.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        out.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* cat = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        out.category = cat ? cat : "general";
        const char* sk = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        out.session_key = sk ? sk : "";
        const char* tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        out.tags = tags ? tags : "";
        out.created_at = sqlite3_column_int64(stmt, 5);
        out.updated_at = sqlite3_column_int64(stmt, 6);
        out.expires_at = sqlite3_column_int64(stmt, 7);
        out.importance = sqlite3_column_int(stmt, 8);
        sqlite3_finalize(stmt);
        LOG_DEBUG("[MemoryEntryStore] Found: %s (category=%s)", id.c_str(), out.category.c_str());
        return true;
    }
    
    sqlite3_finalize(stmt);
    LOG_DEBUG("[MemoryEntryStore] Not found: %s", id.c_str());
    return false;
}

std::vector<MemoryEntry> MemoryEntryStore::list(const std::string& category, int limit) {
    std::vector<MemoryEntry> result;
    if (!db_) return result;
    
    LOG_DEBUG("[MemoryEntryStore] Listing: category='%s', limit=%d",
              category.empty() ? "all" : category.c_str(), limit);
    
    std::string sql = "SELECT id, content, category, session_key, tags, "
                      "created_at, updated_at, expires_at, importance "
                      "FROM memories";
    if (!category.empty()) {
        sql += " WHERE category = ?";
    }
    sql += " ORDER BY importance DESC, updated_at DESC LIMIT ?";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return result;
    
    int bind_idx = 1;
    if (!category.empty()) {
        sqlite3_bind_text(stmt, bind_idx++, category.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, bind_idx, limit);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MemoryEntry e;
        e.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        e.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* cat = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        e.category = cat ? cat : "general";
        const char* sk = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        e.session_key = sk ? sk : "";
        const char* tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        e.tags = tags ? tags : "";
        e.created_at = sqlite3_column_int64(stmt, 5);
        e.updated_at = sqlite3_column_int64(stmt, 6);
        e.expires_at = sqlite3_column_int64(stmt, 7);
        e.importance = sqlite3_column_int(stmt, 8);
        result.push_back(e);
    }
    
    sqlite3_finalize(stmt);
    LOG_DEBUG("[MemoryEntryStore] Listed %zu entries", result.size());
    return result;
}

int MemoryEntryStore::count(const std::string& category) {
    if (!db_) return 0;
    
    LOG_DEBUG("[MemoryEntryStore] Counting: category='%s'",
              category.empty() ? "all" : category.c_str());
    
    std::string sql = "SELECT COUNT(*) FROM memories";
    if (!category.empty()) {
        sql += " WHERE category = ?";
    }
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return 0;
    
    if (!category.empty()) {
        sqlite3_bind_text(stmt, 1, category.c_str(), -1, SQLITE_TRANSIENT);
    }
    
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    LOG_DEBUG("[MemoryEntryStore] Count: %d", count);
    return count;
}

std::vector<MemoryEntry> MemoryEntryStore::search(const std::string& query, int limit) {
    std::vector<MemoryEntry> result;
    if (!db_ || query.empty()) return result;
    
    LOG_DEBUG("[MemoryEntryStore] Searching: query='%s', limit=%d", query.c_str(), limit);
    
    // Try FTS search first
    const char* fts_sql = 
        "SELECT m.id, m.content, m.category, m.session_key, m.tags, "
        "m.created_at, m.updated_at, m.expires_at, m.importance "
        "FROM memories m "
        "JOIN memories_fts f ON m.id = f.memory_id "
        "WHERE memories_fts MATCH ? "
        "ORDER BY m.importance DESC "
        "LIMIT ?";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, fts_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, limit);
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            MemoryEntry e;
            e.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            e.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* cat = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            e.category = cat ? cat : "general";
            const char* sk = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            e.session_key = sk ? sk : "";
            const char* tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            e.tags = tags ? tags : "";
            e.created_at = sqlite3_column_int64(stmt, 5);
            e.updated_at = sqlite3_column_int64(stmt, 6);
            e.expires_at = sqlite3_column_int64(stmt, 7);
            e.importance = sqlite3_column_int(stmt, 8);
            result.push_back(e);
        }
        sqlite3_finalize(stmt);
        
        if (!result.empty()) {
            LOG_DEBUG("[MemoryEntryStore] FTS search found %zu entries", result.size());
            return result;
        }
    } else if (stmt) {
        sqlite3_finalize(stmt);
    }
    
    // Fallback: LIKE search
    const char* like_sql = 
        "SELECT id, content, category, session_key, tags, "
        "created_at, updated_at, expires_at, importance "
        "FROM memories WHERE content LIKE ? OR tags LIKE ? "
        "ORDER BY importance DESC, updated_at DESC LIMIT ?";
    
    stmt = nullptr;
    if (sqlite3_prepare_v2(db_, like_sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    
    std::string like_query = "%" + query + "%";
    sqlite3_bind_text(stmt, 1, like_query.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, like_query.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, limit);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MemoryEntry e;
        e.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        e.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* cat = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        e.category = cat ? cat : "general";
        const char* sk = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        e.session_key = sk ? sk : "";
        const char* tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        e.tags = tags ? tags : "";
        e.created_at = sqlite3_column_int64(stmt, 5);
        e.updated_at = sqlite3_column_int64(stmt, 6);
        e.expires_at = sqlite3_column_int64(stmt, 7);
        e.importance = sqlite3_column_int(stmt, 8);
        result.push_back(e);
    }
    
    sqlite3_finalize(stmt);
    LOG_DEBUG("[MemoryEntryStore] LIKE search found %zu entries", result.size());
    return result;
}

bool MemoryEntryStore::cleanup_expired() {
    if (!db_) return false;
    
    int64_t now = static_cast<int64_t>(time(nullptr)) * 1000;
    LOG_DEBUG("[MemoryEntryStore] Cleaning up expired entries (now=%lld)", now);
    
    // Delete expired FTS entries first
    {
        const char* fts_sql = 
            "DELETE FROM memories_fts WHERE memory_id IN "
            "(SELECT id FROM memories WHERE expires_at > 0 AND expires_at <= ?)";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, fts_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, now);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    
    const char* sql = "DELETE FROM memories WHERE expires_at > 0 AND expires_at <= ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_int64(stmt, 1, now);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    LOG_DEBUG("[MemoryEntryStore] Cleanup completed (success=%s)", (rc == SQLITE_DONE) ? "true" : "false");
    return rc == SQLITE_DONE;
}

// ---- MemoryEntryStore helpers ----

std::string MemoryEntryStore::last_error() const {
    return last_error_;
}

bool MemoryEntryStore::exec(const std::string& sql) {
    std::string error;
    return exec(sql, error);
}

bool MemoryEntryStore::exec(const std::string& sql, std::string& error) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg) {
            error = err_msg;
            last_error_ = err_msg;
            sqlite3_free(err_msg);
        }
        return false;
    }
    return true;
}

void MemoryEntryStore::set_error(const std::string& error) {
    last_error_ = error;
}

void MemoryEntryStore::set_error_from_db() {
    if (db_) {
        last_error_ = sqlite3_errmsg(db_);
    }
}


// ============================================================================
// TaskStore implementation - DATABASE ONLY
// ============================================================================

TaskStore::TaskStore(sqlite3*& db)
    : db_(db)
{
}

bool TaskStore::ensure_schema() {
    if (!db_) {
        set_error("Database not open");
        return false;
    }
    
    if (!exec(
        "CREATE TABLE IF NOT EXISTS tasks ("
        "  id TEXT PRIMARY KEY,"
        "  content TEXT NOT NULL,"
        "  context TEXT,"
        "  channel TEXT,"
        "  user_id TEXT,"
        "  created_at INTEGER,"
        "  due_at INTEGER,"
        "  completed INTEGER DEFAULT 0,"
        "  completed_at INTEGER"
        ")"
    )) return false;
    
    if (!exec("CREATE INDEX IF NOT EXISTS idx_tasks_due ON tasks(due_at)")) return false;
    if (!exec("CREATE INDEX IF NOT EXISTS idx_tasks_completed ON tasks(completed)")) return false;
    
    return true;
}

bool TaskStore::upsert(const MemoryTask& task) {
    if (!db_) {
        set_error("Database not open");
        return false;
    }
    
    LOG_DEBUG("[TaskStore] Upserting: id=%s, content='%.50s', completed=%d",
              task.id.c_str(), task.content.c_str(), task.completed ? 1 : 0);
    
    const char* sql = 
        "INSERT OR REPLACE INTO tasks (id, content, context, channel, user_id, created_at, due_at, completed, completed_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        set_error_from_db();
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, task.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, task.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, task.context.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, task.channel.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, task.user_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, task.created_at);
    sqlite3_bind_int64(stmt, 7, task.due_at);
    sqlite3_bind_int(stmt, 8, task.completed ? 1 : 0);
    sqlite3_bind_int64(stmt, 9, task.completed_at);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        set_error_from_db();
        return false;
    }
    
    return true;
}

bool TaskStore::remove(const std::string& id) {
    if (!db_) return false;
    
    LOG_DEBUG("[TaskStore] Removing: %s", id.c_str());
    
    const char* sql = "DELETE FROM tasks WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool TaskStore::get(const std::string& id, MemoryTask& out) {
    if (!db_) return false;
    
    LOG_DEBUG("[TaskStore] Getting: %s", id.c_str());
    
    const char* sql = "SELECT id, content, context, channel, user_id, created_at, due_at, completed, completed_at FROM tasks WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        out.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        out.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* ctx = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        out.context = ctx ? ctx : "";
        const char* ch = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        out.channel = ch ? ch : "";
        const char* uid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        out.user_id = uid ? uid : "";
        out.created_at = sqlite3_column_int64(stmt, 5);
        out.due_at = sqlite3_column_int64(stmt, 6);
        out.completed = sqlite3_column_int(stmt, 7) != 0;
        out.completed_at = sqlite3_column_int64(stmt, 8);
        sqlite3_finalize(stmt);
        return true;
    }
    
    sqlite3_finalize(stmt);
    return false;
}

std::vector<MemoryTask> TaskStore::list(bool include_completed) {
    std::vector<MemoryTask> result;
    if (!db_) return result;
    
    LOG_DEBUG("[TaskStore] Listing: include_completed=%d", include_completed ? 1 : 0);
    
    std::string sql = "SELECT id, content, context, channel, user_id, created_at, due_at, completed, completed_at FROM tasks";
    if (!include_completed) {
        sql += " WHERE completed = 0";
    }
    sql += " ORDER BY due_at ASC, created_at ASC";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return result;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MemoryTask t;
        t.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        t.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* ctx = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        t.context = ctx ? ctx : "";
        const char* ch = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        t.channel = ch ? ch : "";
        const char* uid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        t.user_id = uid ? uid : "";
        t.created_at = sqlite3_column_int64(stmt, 5);
        t.due_at = sqlite3_column_int64(stmt, 6);
        t.completed = sqlite3_column_int(stmt, 7) != 0;
        t.completed_at = sqlite3_column_int64(stmt, 8);
        result.push_back(t);
    }
    
    sqlite3_finalize(stmt);
    LOG_DEBUG("[TaskStore] Listed %zu tasks", result.size());
    return result;
}

std::vector<MemoryTask> TaskStore::get_pending() {
    return list(false);
}

std::vector<MemoryTask> TaskStore::get_due_before(int64_t timestamp) {
    std::vector<MemoryTask> result;
    if (!db_) return result;
    
    LOG_DEBUG("[TaskStore] Getting tasks due before: %lld", timestamp);
    
    const char* sql = "SELECT id, content, context, channel, user_id, created_at, due_at, completed, completed_at "
                      "FROM tasks WHERE completed = 0 AND due_at > 0 AND due_at <= ? ORDER BY due_at ASC";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    
    sqlite3_bind_int64(stmt, 1, timestamp);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MemoryTask t;
        t.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        t.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* ctx = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        t.context = ctx ? ctx : "";
        const char* ch = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        t.channel = ch ? ch : "";
        const char* uid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        t.user_id = uid ? uid : "";
        t.created_at = sqlite3_column_int64(stmt, 5);
        t.due_at = sqlite3_column_int64(stmt, 6);
        t.completed = sqlite3_column_int(stmt, 7) != 0;
        t.completed_at = sqlite3_column_int64(stmt, 8);
        result.push_back(t);
    }
    
    sqlite3_finalize(stmt);
    LOG_DEBUG("[TaskStore] Found %zu tasks due before deadline", result.size());
    return result;
}

bool TaskStore::complete(const std::string& id) {
    if (!db_) return false;
    
    LOG_DEBUG("[TaskStore] Completing: %s", id.c_str());
    
    int64_t now = static_cast<int64_t>(time(nullptr)) * 1000;
    
    const char* sql = "UPDATE tasks SET completed = 1, completed_at = ? WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_int64(stmt, 1, now);
    sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

// ---- TaskStore helpers ----

std::string TaskStore::last_error() const {
    return last_error_;
}

bool TaskStore::exec(const std::string& sql) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg) {
            last_error_ = err_msg;
            sqlite3_free(err_msg);
        }
        return false;
    }
    return true;
}

void TaskStore::set_error(const std::string& error) {
    last_error_ = error;
}

void TaskStore::set_error_from_db() {
    if (db_) {
        last_error_ = sqlite3_errmsg(db_);
    }
}


// ============================================================================
// MemoryStore implementation - Owns sqlite3 handle, delegates to sub-stores
// ============================================================================

MemoryStore::MemoryStore() 
    : db_(nullptr)
    , files_(db_)
    , memories_(db_)
    , tasks_(db_)
{
}

MemoryStore::~MemoryStore() {
    close();
}

bool MemoryStore::open(const std::string& db_path) {
    if (db_) {
        close();
    }
    
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        set_error_from_db();
        db_ = nullptr;
        return false;
    }
    
    // Enable WAL mode for better concurrency
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA synchronous=NORMAL");
    
    return true;
}

void MemoryStore::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool MemoryStore::is_open() const {
    return db_ != nullptr;
}

bool MemoryStore::ensure_schema() {
    if (!db_) {
        set_error("Database not open");
        return false;
    }
    
    // Meta table (shared)
    if (!exec(
        "CREATE TABLE IF NOT EXISTS meta ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT"
        ")"
    )) return false;
    
    // Initialize each sub-store's schema
    if (!files_.ensure_schema()) return false;
    if (!memories_.ensure_schema()) return false;
    if (!tasks_.ensure_schema()) return false;
    
    return true;
}

// ---- Delegated File operations ----

bool MemoryStore::upsert_file(const MemoryFile& file) { return files_.upsert_file(file); }
bool MemoryStore::delete_file(const std::string& path, MemorySource source) { return files_.delete_file(path, source); }
bool MemoryStore::get_file(const std::string& path, MemorySource source, MemoryFile& out) { return files_.get_file(path, source, out); }
std::vector<MemoryFile> MemoryStore::list_files(MemorySource source) { return files_.list_files(source); }
std::vector<std::string> MemoryStore::get_stale_paths(const std::vector<std::string>& active_paths, MemorySource source) { return files_.get_stale_paths(active_paths, source); }
bool MemoryStore::upsert_chunk(const MemoryChunk& chunk) { return files_.upsert_chunk(chunk); }
bool MemoryStore::delete_chunks_for_file(const std::string& path, MemorySource source) { return files_.delete_chunks_for_file(path, source); }
std::vector<MemoryChunk> MemoryStore::get_chunks_for_file(const std::string& path, MemorySource source) { return files_.get_chunks_for_file(path, source); }
int MemoryStore::count_chunks(MemorySource source) { return files_.count_chunks(source); }
std::vector<MemorySearchResult> MemoryStore::search(const std::string& query, const MemorySearchConfig& config) { return files_.search(query, config); }

// ---- Delegated Memory operations ----

bool MemoryStore::upsert_memory(const MemoryEntry& entry) { return memories_.upsert(entry); }
bool MemoryStore::delete_memory(const std::string& id) { return memories_.remove(id); }
bool MemoryStore::get_memory(const std::string& id, MemoryEntry& out) { return memories_.get(id, out); }
std::vector<MemoryEntry> MemoryStore::list_memories(const std::string& category, int limit) { return memories_.list(category, limit); }
std::vector<MemoryEntry> MemoryStore::search_memories(const std::string& query, int limit) { return memories_.search(query, limit); }
bool MemoryStore::cleanup_expired_memories() { return memories_.cleanup_expired(); }
int MemoryStore::count_memories(const std::string& category) { return memories_.count(category); }

// ---- Delegated Task operations ----

bool MemoryStore::upsert_task(const MemoryTask& task) { return tasks_.upsert(task); }
bool MemoryStore::delete_task(const std::string& id) { return tasks_.remove(id); }
bool MemoryStore::get_task(const std::string& id, MemoryTask& out) { return tasks_.get(id, out); }
std::vector<MemoryTask> MemoryStore::list_tasks(bool include_completed) { return tasks_.list(include_completed); }
std::vector<MemoryTask> MemoryStore::get_pending_tasks() { return tasks_.get_pending(); }
std::vector<MemoryTask> MemoryStore::get_tasks_due_before(int64_t timestamp) { return tasks_.get_due_before(timestamp); }
bool MemoryStore::complete_task(const std::string& id) { return tasks_.complete(id); }

// ---- Meta operations (shared) ----

bool MemoryStore::set_meta(const std::string& key, const std::string& value) {
    if (!db_) return false;
    
    const char* sql = "INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

std::string MemoryStore::get_meta(const std::string& key, const std::string& default_val) {
    if (!db_) return default_val;
    
    const char* sql = "SELECT value FROM meta WHERE key = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return default_val;
    
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    
    std::string result = default_val;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (val) result = val;
    }
    
    sqlite3_finalize(stmt);
    return result;
}

// ---- MemoryStore helpers ----

std::string MemoryStore::last_error() const {
    return last_error_;
}

bool MemoryStore::exec(const std::string& sql) {
    std::string error;
    return exec(sql, error);
}

bool MemoryStore::exec(const std::string& sql, std::string& error) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg) {
            error = err_msg;
            last_error_ = err_msg;
            sqlite3_free(err_msg);
        }
        return false;
    }
    return true;
}

void MemoryStore::set_error(const std::string& error) {
    last_error_ = error;
}

void MemoryStore::set_error_from_db() {
    if (db_) {
        last_error_ = sqlite3_errmsg(db_);
    }
}

} // namespace opencrank
