/*
 * OpenCrank C++11 - Memory Manager
 * 
 * High-level interface for memory operations.
 * Handles file discovery, chunking, indexing, and search.
 * Supports both memory files and session transcripts.
 */
#ifndef OPENCRANK_MEMORY_MANAGER_HPP
#define OPENCRANK_MEMORY_MANAGER_HPP

#include "types.hpp"
#include "store.hpp"
#include <string>
#include <vector>
#include <memory>
#include <set>
#include <map>

namespace opencrank {

class MemoryManager {
public:
    explicit MemoryManager(const MemoryConfig& config);
    ~MemoryManager();
    
    // Initialize the memory system
    bool initialize();
    void shutdown();
    bool is_initialized() const;
    
    // Sync operations - scan and index memory/session files
    bool sync();
    bool sync(const std::string& reason, bool force = false);
    
    // Warm session - sync on session start if configured
    void warm_session(const std::string& session_key = "");
    
    // Memory file operations
    bool save_memory(const std::string& content, const std::string& filename = "");
    bool save_daily_memory(const std::string& content);  // memory/YYYY-MM-DD.md
    std::string load_memory_file(const std::string& path);
    bool append_to_memory(const std::string& content, const std::string& filename = "MEMORY.md");
    std::vector<std::string> list_memory_files();
    
    // Search operations
    std::vector<MemorySearchResult> search(const std::string& query);
    std::vector<MemorySearchResult> search(const std::string& query, const MemorySearchConfig& config);
    std::vector<MemorySearchResult> search(const std::string& query, 
                                           const MemorySearchConfig& config,
                                           const std::string& session_key);
    
    // Read file content (for memory_get tool)
    struct ReadFileResult {
        std::string text;
        std::string path;
        bool success;
        std::string error;
    };
    ReadFileResult read_file(const std::string& rel_path, int from_line = 0, int num_lines = 0);
    
    // Get file content by path (legacy)
    std::string get_memory_content(const std::string& path);
    
    // Task operations
    std::string create_task(const std::string& content, const std::string& context = "");
    bool complete_task(const std::string& task_id);
    std::vector<MemoryTask> list_tasks(bool include_completed = false);
    std::vector<MemoryTask> get_pending_tasks();
    std::vector<MemoryTask> get_tasks_due_soon(int hours = 24);
    bool update_task_due(const std::string& task_id, int64_t due_at);
    
    // Status
    struct MemoryStatus {
        std::string backend;        // "builtin"
        std::string provider;       // "bm25" (no vectors yet)
        int files;
        int chunks;
        bool dirty;
        bool sessions_dirty;
        std::string workspace_dir;
        std::string db_path;
        std::vector<std::string> sources;
    };
    MemoryStatus status() const;
    
    // Check if dirty (needs sync)
    bool is_dirty() const { return dirty_ || sessions_dirty_; }
    
    // Utility
    std::string last_error() const;
    const MemoryConfig& config() const;
    
private:
    MemoryConfig config_;
    std::unique_ptr<MemoryStore> store_;
    bool initialized_;
    bool dirty_;
    bool sessions_dirty_;
    std::string last_error_;
    std::set<std::string> session_warm_;  // Session keys that have been warmed
    std::set<std::string> sessions_dirty_files_;  // Session files that need re-indexing
    
    // Session delta tracking
    struct SessionDelta {
        int64_t last_size;
        int pending_bytes;
        int pending_messages;
    };
    std::map<std::string, SessionDelta> session_deltas_;
    
    // File discovery
    std::vector<MemoryFile> discover_memory_files();
    MemoryFile build_file_entry(const std::string& abs_path);
    
    // Session file operations
    std::vector<std::string> list_session_files();
    SessionFileEntry build_session_entry(const std::string& abs_path);
    std::string extract_session_text(const std::string& jsonl_content);
    std::string normalize_session_text(const std::string& text);
    bool sync_session_files();
    bool should_sync_sessions(const std::string& reason, bool force);
    
    // Indexing
    bool index_file(const MemoryFile& file);
    bool index_session_file(const SessionFileEntry& entry);
    std::vector<MemoryChunk> chunk_content(const std::string& content, 
                                           const std::string& path,
                                           MemorySource source);
    
    // Chunking helpers
    std::vector<std::string> split_into_paragraphs(const std::string& content);
    std::string compute_hash(const std::string& content);
    std::string generate_uuid();
    
    // Text helpers
    std::string unescape_json_string(const std::string& s);
    std::string load_file_content(const std::string& abs_path);
    
    // Path helpers
    std::string resolve_session_transcripts_dir();
    std::string session_path_for_file(const std::string& abs_path);
    bool is_memory_path(const std::string& rel_path);
    bool is_allowed_path(const std::string& abs_path);
    
    // Citation helpers
    bool should_include_citations(MemoryCitationMode mode, const std::string& session_key);
    void decorate_citations(std::vector<MemorySearchResult>& results, bool include);
    std::string derive_chat_type_from_session_key(const std::string& session_key);
    
    // Date helpers
    std::string get_today_date();
    int64_t get_current_timestamp();
    
    void set_error(const std::string& error);
};

} // namespace opencrank

#endif // OPENCRANK_MEMORY_MANAGER_HPP
