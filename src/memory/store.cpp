/*
 * OpenCrank C++ - Memory Store Implementation
 * 
 * SQLite storage backend for memories and tasks.
 * Uses FTS5 for BM25 full-text search.
 */
#include <opencrank/memory/store.hpp>
#include <opencrank/core/logger.hpp>
#include <opencrank/core/utils.hpp>
#include <sqlite3.h>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

namespace opencrank {

// ============================================================================
// Helpers
// ============================================================================

int64_t MemoryStore::now_ms() {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    int64_t t = (static_cast<int64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return (t / 10000) - 11644473600000LL;
#else
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
#endif
}

std::string MemoryStore::generate_uuid() {
    // Simple UUID v4 generation using random bytes
    unsigned char bytes[16];
    
    // Seed from time + pid for reasonable uniqueness
    static bool seeded = false;
    if (!seeded) {
        srand(static_cast<unsigned>(time(nullptr)) ^ static_cast<unsigned>(getpid()));
        seeded = true;
    }
    
    for (int i = 0; i < 16; ++i) {
        bytes[i] = static_cast<unsigned char>(rand() % 256);
    }
    
    // Set version (4) and variant (10xx)
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;
    
    char buf[37];
    snprintf(buf, sizeof(buf),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15]);
    
    return std::string(buf);
}

// ============================================================================
// MemoryStore Implementation
// ============================================================================

MemoryStore::MemoryStore() : db_(nullptr) {}

MemoryStore::~MemoryStore() {
    close();
}

bool MemoryStore::open(const std::string& db_path) {
    if (db_) {
        close();
    }
    
    // Ensure parent directory exists
    if (!opencrank::create_parent_directory(db_path)) {
        LOG_ERROR("[MemoryStore] Failed to create parent directory for '%s'", db_path.c_str());
        return false;
    }
    
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        LOG_ERROR("[MemoryStore] Failed to open database '%s': %s",
                  db_path.c_str(), sqlite3_errmsg(db_));
        db_ = nullptr;
        return false;
    }
    
    // Enable WAL mode for better concurrent access
    exec_sql("PRAGMA journal_mode=WAL");
    exec_sql("PRAGMA synchronous=NORMAL");
    exec_sql("PRAGMA busy_timeout=5000");
    
    if (!init_tables()) {
        LOG_ERROR("[MemoryStore] Failed to initialize tables");
        close();
        return false;
    }
    
    LOG_INFO("[MemoryStore] Database opened: %s", db_path.c_str());
    return true;
}

void MemoryStore::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool MemoryStore::exec_sql(const std::string& sql) {
    if (!db_) return false;
    
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    
    if (rc != SQLITE_OK) {
        LOG_ERROR("[MemoryStore] SQL error: %s\n  Query: %s",
                  err_msg ? err_msg : "unknown", sql.c_str());
        if (err_msg) sqlite3_free(err_msg);
        return false;
    }
    
    return true;
}

bool MemoryStore::init_tables() {
    // Main memories table
    bool ok = exec_sql(
        "CREATE TABLE IF NOT EXISTS memories ("
        "  id TEXT PRIMARY KEY,"
        "  content TEXT NOT NULL,"
        "  category TEXT DEFAULT 'general',"
        "  tags TEXT DEFAULT '',"
        "  channel TEXT DEFAULT '',"
        "  user_id TEXT DEFAULT '',"
        "  importance INTEGER DEFAULT 5,"
        "  created_at INTEGER NOT NULL,"
        "  updated_at INTEGER NOT NULL"
        ")"
    );
    if (!ok) return false;
    
    // FTS5 virtual table for full-text search on memories
    ok = exec_sql(
        "CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts USING fts5("
        "  content, category, tags,"
        "  content_rowid='rowid',"
        "  tokenize='porter unicode61'"
        ")"
    );
    if (!ok) return false;
    
    // Triggers to keep FTS index in sync with memories table
    // We use the rowid from memories implicitly via the id column
    // But since our id is TEXT, we need a mapping. FTS5 content_rowid
    // requires an integer rowid, so we use the implicit rowid.
    
    // Insert trigger
    exec_sql(
        "CREATE TRIGGER IF NOT EXISTS memories_ai AFTER INSERT ON memories BEGIN "
        "  INSERT INTO memories_fts(rowid, content, category, tags) "
        "    VALUES (NEW.rowid, NEW.content, NEW.category, NEW.tags);"
        "END"
    );
    
    // Update trigger
    exec_sql(
        "CREATE TRIGGER IF NOT EXISTS memories_au AFTER UPDATE ON memories BEGIN "
        "  DELETE FROM memories_fts WHERE rowid = OLD.rowid;"
        "  INSERT INTO memories_fts(rowid, content, category, tags) "
        "    VALUES (NEW.rowid, NEW.content, NEW.category, NEW.tags);"
        "END"
    );
    
    // Delete trigger
    exec_sql(
        "CREATE TRIGGER IF NOT EXISTS memories_ad AFTER DELETE ON memories BEGIN "
        "  DELETE FROM memories_fts WHERE rowid = OLD.rowid;"
        "END"
    );
    
    // Tasks table
    ok = exec_sql(
        "CREATE TABLE IF NOT EXISTS tasks ("
        "  id TEXT PRIMARY KEY,"
        "  content TEXT NOT NULL,"
        "  context TEXT DEFAULT '',"
        "  channel TEXT DEFAULT '',"
        "  user_id TEXT DEFAULT '',"
        "  created_at INTEGER NOT NULL,"
        "  due_at INTEGER DEFAULT 0,"
        "  completed INTEGER DEFAULT 0,"
        "  completed_at INTEGER DEFAULT 0"
        ")"
    );
    if (!ok) return false;
    
    // Index for task queries
    exec_sql("CREATE INDEX IF NOT EXISTS idx_tasks_completed ON tasks(completed)");
    exec_sql("CREATE INDEX IF NOT EXISTS idx_tasks_due ON tasks(due_at)");
    exec_sql("CREATE INDEX IF NOT EXISTS idx_tasks_channel ON tasks(channel)");
    
    // Index for memory queries
    exec_sql("CREATE INDEX IF NOT EXISTS idx_memories_category ON memories(category)");
    exec_sql("CREATE INDEX IF NOT EXISTS idx_memories_updated ON memories(updated_at)");
    
    LOG_DEBUG("[MemoryStore] Tables initialized");
    return true;
}

// ============================================================================
// Memory Operations
// ============================================================================

bool MemoryStore::save_memory(const MemoryEntry& entry) {
    if (!db_) return false;
    
    std::string id = entry.id.empty() ? generate_uuid() : entry.id;
    int64_t now = now_ms();
    int64_t created = entry.created_at > 0 ? entry.created_at : now;
    
    // Use INSERT OR REPLACE to handle both insert and update
    const char* sql = 
        "INSERT OR REPLACE INTO memories "
        "(id, content, category, tags, channel, user_id, importance, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("[MemoryStore] save_memory prepare failed: %s", sqlite3_errmsg(db_));
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, entry.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, entry.category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, entry.tags.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, entry.channel.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, entry.user_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, entry.importance);
    sqlite3_bind_int64(stmt, 8, created);
    sqlite3_bind_int64(stmt, 9, now);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        LOG_ERROR("[MemoryStore] save_memory step failed: %s", sqlite3_errmsg(db_));
        return false;
    }
    
    LOG_DEBUG("[MemoryStore] Saved memory id=%s category=%s importance=%d",
              id.c_str(), entry.category.c_str(), entry.importance);
    return true;
}

std::vector<MemorySearchHit> MemoryStore::search_memories(
    const std::string& query, int max_results, const std::string& category_filter)
{
    std::vector<MemorySearchHit> results;
    if (!db_ || query.empty()) return results;
    
    // Build FTS5 query with BM25 ranking
    // Join memories_fts with memories to get full row data
    std::string sql;
    if (category_filter.empty()) {
        sql = 
            "SELECT m.id, m.content, m.category, m.tags, m.channel, m.user_id, "
            "       m.importance, m.created_at, m.updated_at, "
            "       bm25(memories_fts, 1.0, 0.5, 0.3) AS score, "
            "       snippet(memories_fts, 0, '<b>', '</b>', '...', 64) AS snip "
            "FROM memories_fts f "
            "JOIN memories m ON m.rowid = f.rowid "
            "WHERE memories_fts MATCH ? "
            "ORDER BY score "
            "LIMIT ?";
    } else {
        sql = 
            "SELECT m.id, m.content, m.category, m.tags, m.channel, m.user_id, "
            "       m.importance, m.created_at, m.updated_at, "
            "       bm25(memories_fts, 1.0, 0.5, 0.3) AS score, "
            "       snippet(memories_fts, 0, '<b>', '</b>', '...', 64) AS snip "
            "FROM memories_fts f "
            "JOIN memories m ON m.rowid = f.rowid "
            "WHERE memories_fts MATCH ? AND m.category = ? "
            "ORDER BY score "
            "LIMIT ?";
    }
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("[MemoryStore] search_memories prepare failed: %s", sqlite3_errmsg(db_));
        return results;
    }
    
    // Sanitize the query for FTS5: wrap each word in quotes to avoid syntax errors
    std::string safe_query;
    {
        std::istringstream iss(query);
        std::string word;
        bool first = true;
        while (iss >> word) {
            // Strip characters that are special in FTS5
            std::string clean;
            for (size_t i = 0; i < word.size(); ++i) {
                char c = word[i];
                if (c != '"' && c != '\'' && c != '*' && c != '(' && c != ')') {
                    clean += c;
                }
            }
            if (clean.empty()) continue;
            
            if (!first) safe_query += " OR ";
            safe_query += "\"" + clean + "\"";
            first = false;
        }
    }
    
    if (safe_query.empty()) {
        sqlite3_finalize(stmt);
        return results;
    }
    
    sqlite3_bind_text(stmt, 1, safe_query.c_str(), -1, SQLITE_TRANSIENT);
    
    if (category_filter.empty()) {
        sqlite3_bind_int(stmt, 2, max_results);
    } else {
        sqlite3_bind_text(stmt, 2, category_filter.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, max_results);
    }
    
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        MemorySearchHit hit;
        
        const char* col_text;
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        hit.entry.id = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        hit.entry.content = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        hit.entry.category = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        hit.entry.tags = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        hit.entry.channel = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        hit.entry.user_id = col_text ? col_text : "";
        
        hit.entry.importance = sqlite3_column_int(stmt, 6);
        hit.entry.created_at = sqlite3_column_int64(stmt, 7);
        hit.entry.updated_at = sqlite3_column_int64(stmt, 8);
        
        hit.score = sqlite3_column_double(stmt, 9);
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        hit.snippet = col_text ? col_text : "";
        
        results.push_back(hit);
    }
    
    sqlite3_finalize(stmt);
    
    LOG_DEBUG("[MemoryStore] Search '%s' returned %zu results", 
              query.c_str(), results.size());
    return results;
}

MemoryEntry MemoryStore::get_memory(const std::string& id) {
    MemoryEntry entry;
    if (!db_ || id.empty()) return entry;
    
    const char* sql = 
        "SELECT id, content, category, tags, channel, user_id, "
        "       importance, created_at, updated_at "
        "FROM memories WHERE id = ?";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return entry;
    
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* col_text;
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        entry.id = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        entry.content = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        entry.category = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        entry.tags = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        entry.channel = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        entry.user_id = col_text ? col_text : "";
        
        entry.importance = sqlite3_column_int(stmt, 6);
        entry.created_at = sqlite3_column_int64(stmt, 7);
        entry.updated_at = sqlite3_column_int64(stmt, 8);
    }
    
    sqlite3_finalize(stmt);
    return entry;
}

std::vector<MemoryEntry> MemoryStore::get_recent_memories(
    int limit, const std::string& category_filter)
{
    std::vector<MemoryEntry> results;
    if (!db_) return results;
    
    std::string sql;
    if (category_filter.empty()) {
        sql = "SELECT id, content, category, tags, channel, user_id, "
              "       importance, created_at, updated_at "
              "FROM memories ORDER BY updated_at DESC LIMIT ?";
    } else {
        sql = "SELECT id, content, category, tags, channel, user_id, "
              "       importance, created_at, updated_at "
              "FROM memories WHERE category = ? ORDER BY updated_at DESC LIMIT ?";
    }
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return results;
    
    if (category_filter.empty()) {
        sqlite3_bind_int(stmt, 1, limit);
    } else {
        sqlite3_bind_text(stmt, 1, category_filter.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, limit);
    }
    
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        MemoryEntry entry;
        const char* col_text;
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        entry.id = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        entry.content = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        entry.category = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        entry.tags = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        entry.channel = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        entry.user_id = col_text ? col_text : "";
        
        entry.importance = sqlite3_column_int(stmt, 6);
        entry.created_at = sqlite3_column_int64(stmt, 7);
        entry.updated_at = sqlite3_column_int64(stmt, 8);
        
        results.push_back(entry);
    }
    
    sqlite3_finalize(stmt);
    return results;
}

bool MemoryStore::delete_memory(const std::string& id) {
    if (!db_ || id.empty()) return false;
    
    const char* sql = "DELETE FROM memories WHERE id = ?";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

// ============================================================================
// Task Operations
// ============================================================================

bool MemoryStore::create_task(const MemoryTask& task) {
    if (!db_) return false;
    
    std::string id = task.id.empty() ? generate_uuid() : task.id;
    int64_t now = now_ms();
    int64_t created = task.created_at > 0 ? task.created_at : now;
    
    const char* sql = 
        "INSERT INTO tasks "
        "(id, content, context, channel, user_id, created_at, due_at, completed, completed_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, 0, 0)";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("[MemoryStore] create_task prepare failed: %s", sqlite3_errmsg(db_));
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, task.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, task.context.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, task.channel.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, task.user_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, created);
    sqlite3_bind_int64(stmt, 7, task.due_at);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        LOG_ERROR("[MemoryStore] create_task step failed: %s", sqlite3_errmsg(db_));
        return false;
    }
    
    LOG_DEBUG("[MemoryStore] Created task id=%s content='%.50s'",
              id.c_str(), task.content.c_str());
    return true;
}

std::vector<MemoryTask> MemoryStore::list_tasks(
    bool include_completed, const std::string& channel_filter)
{
    std::vector<MemoryTask> results;
    if (!db_) return results;
    
    // Build query based on filters
    std::ostringstream sql;
    sql << "SELECT id, content, context, channel, user_id, "
        << "       created_at, due_at, completed, completed_at "
        << "FROM tasks";
    
    std::vector<std::string> conditions;
    if (!include_completed) {
        conditions.push_back("completed = 0");
    }
    if (!channel_filter.empty()) {
        conditions.push_back("channel = ?");
    }
    
    if (!conditions.empty()) {
        sql << " WHERE " << conditions[0];
        for (size_t i = 1; i < conditions.size(); ++i) {
            sql << " AND " << conditions[i];
        }
    }
    
    sql << " ORDER BY CASE WHEN due_at > 0 THEN due_at ELSE 9999999999999 END ASC, "
        << "created_at DESC";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.str().c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("[MemoryStore] list_tasks prepare failed: %s", sqlite3_errmsg(db_));
        return results;
    }
    
    // Bind channel filter if present
    // "completed = 0" is a literal condition with no bind parameter,
    // so channel filter is always bind index 1
    if (!channel_filter.empty()) {
        sqlite3_bind_text(stmt, 1, channel_filter.c_str(), -1, SQLITE_TRANSIENT);
    }
    
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        MemoryTask task;
        const char* col_text;
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        task.id = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        task.content = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        task.context = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        task.channel = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        task.user_id = col_text ? col_text : "";
        
        task.created_at = sqlite3_column_int64(stmt, 5);
        task.due_at = sqlite3_column_int64(stmt, 6);
        task.completed = sqlite3_column_int(stmt, 7) != 0;
        task.completed_at = sqlite3_column_int64(stmt, 8);
        
        results.push_back(task);
    }
    
    sqlite3_finalize(stmt);
    return results;
}

MemoryTask MemoryStore::get_task(const std::string& id) {
    MemoryTask task;
    if (!db_ || id.empty()) return task;
    
    const char* sql = 
        "SELECT id, content, context, channel, user_id, "
        "       created_at, due_at, completed, completed_at "
        "FROM tasks WHERE id = ?";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return task;
    
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* col_text;
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        task.id = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        task.content = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        task.context = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        task.channel = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        task.user_id = col_text ? col_text : "";
        
        task.created_at = sqlite3_column_int64(stmt, 5);
        task.due_at = sqlite3_column_int64(stmt, 6);
        task.completed = sqlite3_column_int(stmt, 7) != 0;
        task.completed_at = sqlite3_column_int64(stmt, 8);
    }
    
    sqlite3_finalize(stmt);
    return task;
}

bool MemoryStore::complete_task(const std::string& id) {
    if (!db_ || id.empty()) return false;
    
    int64_t now = now_ms();
    
    const char* sql = 
        "UPDATE tasks SET completed = 1, completed_at = ? WHERE id = ?";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_int64(stmt, 1, now);
    sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc == SQLITE_DONE) {
        LOG_DEBUG("[MemoryStore] Completed task id=%s", id.c_str());
        return true;
    }
    return false;
}

bool MemoryStore::delete_task(const std::string& id) {
    if (!db_ || id.empty()) return false;
    
    const char* sql = "DELETE FROM tasks WHERE id = ?";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

std::vector<MemoryTask> MemoryStore::get_due_tasks() {
    std::vector<MemoryTask> results;
    if (!db_) return results;
    
    int64_t now = now_ms();
    
    const char* sql = 
        "SELECT id, content, context, channel, user_id, "
        "       created_at, due_at, completed, completed_at "
        "FROM tasks WHERE completed = 0 AND due_at > 0 AND due_at <= ? "
        "ORDER BY due_at ASC";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return results;
    
    sqlite3_bind_int64(stmt, 1, now);
    
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        MemoryTask task;
        const char* col_text;
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        task.id = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        task.content = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        task.context = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        task.channel = col_text ? col_text : "";
        
        col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        task.user_id = col_text ? col_text : "";
        
        task.created_at = sqlite3_column_int64(stmt, 5);
        task.due_at = sqlite3_column_int64(stmt, 6);
        task.completed = sqlite3_column_int(stmt, 7) != 0;
        task.completed_at = sqlite3_column_int64(stmt, 8);
        
        results.push_back(task);
    }
    
    sqlite3_finalize(stmt);
    return results;
}

bool MemoryStore::bind_and_step(sqlite3_stmt* stmt) {
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

} // namespace opencrank
