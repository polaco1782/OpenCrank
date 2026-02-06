/*
 * OpenCrank C++11 - Memory SQLite Store Implementation
 */
#include <opencrank/memory/store.hpp>
#include <cstring>
#include <sstream>

namespace opencrank {

MemoryStore::MemoryStore() 
    : db_(nullptr)
    , fts_available_(false)
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
    
    // Meta table for storing key-value settings
    if (!exec(
        "CREATE TABLE IF NOT EXISTS meta ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT"
        ")"
    )) return false;
    
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
    
    // Tasks table
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
    
    // Try to create FTS5 table for full-text search
    fts_available_ = ensure_fts_table();
    
    return true;
}

bool MemoryStore::ensure_fts_table() {
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

bool MemoryStore::upsert_file(const MemoryFile& file) {
    if (!db_) {
        set_error("Database not open");
        return false;
    }
    
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

bool MemoryStore::delete_file(const std::string& path, MemorySource source) {
    if (!db_) {
        set_error("Database not open");
        return false;
    }
    
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

bool MemoryStore::get_file(const std::string& path, MemorySource source, MemoryFile& out) {
    if (!db_) {
        set_error("Database not open");
        return false;
    }
    
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

std::vector<MemoryFile> MemoryStore::list_files(MemorySource source) {
    std::vector<MemoryFile> result;
    if (!db_) return result;
    
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
    return result;
}

std::vector<std::string> MemoryStore::get_stale_paths(const std::vector<std::string>& active_paths, 
                                                       MemorySource source) {
    std::vector<std::string> stale;
    std::vector<MemoryFile> files = list_files(source);
    
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
    
    return stale;
}

bool MemoryStore::upsert_chunk(const MemoryChunk& chunk) {
    if (!db_) {
        set_error("Database not open");
        return false;
    }
    
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

bool MemoryStore::sync_chunk_to_fts(const MemoryChunk& chunk) {
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

bool MemoryStore::delete_chunk_from_fts(const std::string& chunk_id) {
    const char* sql = "DELETE FROM chunks_fts WHERE chunk_id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, chunk_id.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool MemoryStore::delete_chunks_for_file(const std::string& path, MemorySource source) {
    if (!db_) {
        set_error("Database not open");
        return false;
    }
    
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

std::vector<MemoryChunk> MemoryStore::get_chunks_for_file(const std::string& path, MemorySource source) {
    std::vector<MemoryChunk> result;
    if (!db_) return result;
    
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
    return result;
}

int MemoryStore::count_chunks(MemorySource source) {
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

std::vector<MemorySearchResult> MemoryStore::search(const std::string& query, 
                                                     const MemorySearchConfig& config) {
    std::vector<MemorySearchResult> results;
    if (!db_ || query.empty()) return results;
    
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
                // chunk_id not used but we retrieve it
                r.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                r.source = string_to_memory_source(
                    reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))
                );
                const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                r.snippet = txt ? txt : "";
                // BM25 returns negative scores, lower is better, convert to 0-1 range
                double raw_score = sqlite3_column_double(stmt, 4);
                r.score = 1.0 / (1.0 - raw_score);  // Normalize
                r.start_line = 0;
                r.end_line = 0;
                
                if (r.score >= config.min_score) {
                    results.push_back(r);
                }
            }
            
            sqlite3_finalize(stmt);
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
        r.score = 0.5;  // Fixed score for LIKE matches
        results.push_back(r);
    }
    
    sqlite3_finalize(stmt);
    return results;
}

// Task operations
bool MemoryStore::upsert_task(const MemoryTask& task) {
    if (!db_) {
        set_error("Database not open");
        return false;
    }
    
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

bool MemoryStore::delete_task(const std::string& id) {
    if (!db_) return false;
    
    const char* sql = "DELETE FROM tasks WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool MemoryStore::get_task(const std::string& id, MemoryTask& out) {
    if (!db_) return false;
    
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

std::vector<MemoryTask> MemoryStore::list_tasks(bool include_completed) {
    std::vector<MemoryTask> result;
    if (!db_) return result;
    
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
    return result;
}

std::vector<MemoryTask> MemoryStore::get_pending_tasks() {
    return list_tasks(false);
}

std::vector<MemoryTask> MemoryStore::get_tasks_due_before(int64_t timestamp) {
    std::vector<MemoryTask> result;
    if (!db_) return result;
    
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
    return result;
}

bool MemoryStore::complete_task(const std::string& id) {
    if (!db_) return false;
    
    // Get current time in milliseconds
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

// Meta operations
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
