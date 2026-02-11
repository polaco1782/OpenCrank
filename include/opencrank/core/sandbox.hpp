/*
 * opencrank C++11 - Security Sandbox (Landlock)
 * 
 * Restricts filesystem access for the entire process (including child processes
 * spawned by shell tool) using Linux Landlock LSM.
 * 
 * After activation, the process can only access paths explicitly allowed.
 * This prevents the AI from reading/writing outside its jail, even through
 * shell commands.
 */
#ifndef opencrank_CORE_SANDBOX_HPP
#define opencrank_CORE_SANDBOX_HPP

#include <string>
#include <vector>

namespace opencrank {

class Sandbox {
public:
    // Singleton access
    static Sandbox& instance();

    // Initialize directories (~/.opencrank/db, ~/.opencrank/jail, etc.)
    // Must be called before activate().
    bool init();

    // Activate the Landlock sandbox. After this call, the process can ONLY
    // access the allowed paths. All child processes inherit this restriction.
    // Returns true on success or if already active. Returns false on error
    // (but does NOT abort - caller decides policy).
    bool activate();

    // Whether the sandbox is currently active
    bool is_active() const { return active_; }

    // Whether Landlock is supported on this kernel
    bool is_supported() const { return supported_; }

    // Get the base directory (~/.opencrank)
    const std::string& base_dir() const { return base_dir_; }

    // Get the database directory (~/.opencrank/db)
    const std::string& db_dir() const { return db_dir_; }

    // Get the jail/workspace directory (~/.opencrank/jail)
    const std::string& jail_dir() const { return jail_dir_; }

    // Get the database path for memory.db
    std::string memory_db_path() const;

    // Add an extra path to allow (must be called before activate())
    void allow_path(const std::string& path);

    // Check if a path is within the jail
    bool is_path_in_jail(const std::string& path) const;

    // Check if a path is within allowed sandbox boundaries
    bool is_path_allowed(const std::string& path) const;

    // Resolve a relative path within the jail
    std::string resolve_in_jail(const std::string& relative_path) const;

private:
    Sandbox();
    Sandbox(const Sandbox&);
    Sandbox& operator=(const Sandbox&);

    bool ensure_directory(const std::string& path);
    std::string resolve_home_dir() const;

    bool active_;
    bool supported_;
    std::string base_dir_;    // ~/.opencrank
    std::string db_dir_;      // ~/.opencrank/db
    std::string jail_dir_;    // ~/.opencrank/jail
    std::vector<std::string> extra_allowed_paths_;
};

} // namespace opencrank

#endif // opencrank_CORE_SANDBOX_HPP
