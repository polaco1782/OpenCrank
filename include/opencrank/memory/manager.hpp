/*
 * opencrank C++ - Memory Manager
 * 
 * High-level memory management coordinating the storage backend.
 * Provides a clean interface for saving, searching, and managing
 * memories and tasks through the SQLite store.
 */
#ifndef opencrank_MEMORY_MANAGER_HPP
#define opencrank_MEMORY_MANAGER_HPP

#include "store.hpp"
#include "types.hpp"
#include <string>
#include <vector>

namespace opencrank {

// Forward declaration
class Config;

// ============================================================================
// MemoryManager - High-level Memory Operations
// ============================================================================

class MemoryManager {
public:
    MemoryManager();
    ~MemoryManager();
    
    // Initialize with configuration
    bool init(const MemoryConfig& config);
    bool init(const Config& config);
    
    // Shutdown and release resources
    void shutdown();
    
    // Check if initialized
    bool is_initialized() const { return initialized_; }
    
    // ========================================================================
    // Memory Operations
    // ========================================================================
    
    // Save a memory to the database
    // Returns the memory ID (generated if not provided)
    std::string save_memory(const std::string& content,
                            const std::string& category = "general",
                            int importance = 5,
                            const std::string& tags = "",
                            const std::string& channel = "",
                            const std::string& user_id = "");
    
    // Search memories using BM25 full-text search
    std::vector<MemorySearchHit> search(const std::string& query,
                                         int max_results = 10,
                                         const std::string& category = "");
    
    // Get a specific memory by ID
    MemoryEntry get_memory(const std::string& id);
    
    // Get recent memories
    std::vector<MemoryEntry> get_recent(int limit = 10,
                                         const std::string& category = "");
    
    // Delete a memory
    bool delete_memory(const std::string& id);
    
    // ========================================================================
    // Task Operations
    // ========================================================================
    
    // Create a new task
    // Returns the task ID
    std::string create_task(const std::string& content,
                            const std::string& context = "",
                            int64_t due_at = 0,
                            const std::string& channel = "",
                            const std::string& user_id = "");
    
    // List tasks
    std::vector<MemoryTask> list_tasks(bool include_completed = false,
                                        const std::string& channel = "");
    
    // Get a specific task
    MemoryTask get_task(const std::string& id);
    
    // Complete a task
    bool complete_task(const std::string& id);
    
    // Delete a task
    bool delete_task(const std::string& id);
    
    // Get overdue tasks
    std::vector<MemoryTask> get_due_tasks();
    
    // ========================================================================
    // Access
    // ========================================================================
    
    MemoryStore& store() { return store_; }
    const MemoryStore& store() const { return store_; }
    const MemoryConfig& config() const { return config_; }

private:
    MemoryStore store_;
    MemoryConfig config_;
    bool initialized_;
};

} // namespace opencrank

#endif // opencrank_MEMORY_MANAGER_HPP
