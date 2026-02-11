/*
 * OpenCrank C++11 - Security Sandbox Implementation (Landlock)
 *
 * Uses Linux Landlock LSM to restrict filesystem access for the entire
 * process tree. After activation, neither the main process nor any child
 * process (e.g., shell commands from the AI) can access files outside
 * the allowed directories.
 *
 * Landlock is unprivileged (no root/capabilities needed) and available
 * since Linux 5.13. If unsupported, the sandbox degrades gracefully.
 */
#include <opencrank/core/sandbox.hpp>
#include <opencrank/core/logger.hpp>

#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <climits>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <fcntl.h>

// ============================================================================
// Landlock syscall wrappers (not in glibc until very recently)
// ============================================================================

#ifdef __linux__

#include <linux/landlock.h>
#include <sys/syscall.h>

#ifndef __NR_landlock_create_ruleset
#define __NR_landlock_create_ruleset 444
#endif
#ifndef __NR_landlock_add_rule
#define __NR_landlock_add_rule 445
#endif
#ifndef __NR_landlock_restrict_self
#define __NR_landlock_restrict_self 446
#endif

// Ensure struct definitions exist (for older headers)
#ifndef LANDLOCK_ACCESS_FS_EXECUTE

#define LANDLOCK_ACCESS_FS_EXECUTE          (1ULL << 0)
#define LANDLOCK_ACCESS_FS_WRITE_FILE       (1ULL << 1)
#define LANDLOCK_ACCESS_FS_READ_FILE        (1ULL << 2)
#define LANDLOCK_ACCESS_FS_READ_DIR         (1ULL << 3)
#define LANDLOCK_ACCESS_FS_REMOVE_DIR       (1ULL << 4)
#define LANDLOCK_ACCESS_FS_REMOVE_FILE      (1ULL << 5)
#define LANDLOCK_ACCESS_FS_MAKE_CHAR        (1ULL << 6)
#define LANDLOCK_ACCESS_FS_MAKE_DIR         (1ULL << 7)
#define LANDLOCK_ACCESS_FS_MAKE_REG         (1ULL << 8)
#define LANDLOCK_ACCESS_FS_MAKE_SOCK        (1ULL << 9)
#define LANDLOCK_ACCESS_FS_MAKE_FIFO        (1ULL << 10)
#define LANDLOCK_ACCESS_FS_MAKE_BLOCK       (1ULL << 11)
#define LANDLOCK_ACCESS_FS_MAKE_SYM         (1ULL << 12)

#define LANDLOCK_RULE_PATH_BENEATH 1

struct landlock_ruleset_attr {
    __u64 handled_access_fs;
};

struct landlock_path_beneath_attr {
    __u64 allowed_access;
    __s32 parent_fd;
} __attribute__((packed));

#endif // LANDLOCK_ACCESS_FS_EXECUTE

// All filesystem access rights (Landlock ABI v1)
#define LANDLOCK_ACCESS_FS_ALL ( \
    LANDLOCK_ACCESS_FS_EXECUTE          | \
    LANDLOCK_ACCESS_FS_WRITE_FILE       | \
    LANDLOCK_ACCESS_FS_READ_FILE        | \
    LANDLOCK_ACCESS_FS_READ_DIR         | \
    LANDLOCK_ACCESS_FS_REMOVE_DIR       | \
    LANDLOCK_ACCESS_FS_REMOVE_FILE      | \
    LANDLOCK_ACCESS_FS_MAKE_CHAR        | \
    LANDLOCK_ACCESS_FS_MAKE_DIR         | \
    LANDLOCK_ACCESS_FS_MAKE_REG         | \
    LANDLOCK_ACCESS_FS_MAKE_SOCK        | \
    LANDLOCK_ACCESS_FS_MAKE_FIFO        | \
    LANDLOCK_ACCESS_FS_MAKE_BLOCK       | \
    LANDLOCK_ACCESS_FS_MAKE_SYM         \
)

static inline int landlock_create_ruleset(
    const struct landlock_ruleset_attr* attr,
    size_t size, __u32 flags) {
    return static_cast<int>(syscall(__NR_landlock_create_ruleset, attr, size, flags));
}

static inline int landlock_add_rule(
    int ruleset_fd, enum landlock_rule_type type,
    const void* attr, __u32 flags) {
    return static_cast<int>(syscall(__NR_landlock_add_rule, ruleset_fd, type, attr, flags));
}

static inline int landlock_restrict_self(int ruleset_fd, __u32 flags) {
    return static_cast<int>(syscall(__NR_landlock_restrict_self, ruleset_fd, flags));
}

#endif // __linux__

namespace opencrank {

// ============================================================================
// Sandbox Implementation
// ============================================================================

Sandbox& Sandbox::instance() {
    static Sandbox s;
    return s;
}

Sandbox::Sandbox()
    : active_(false)
    , supported_(false)
{
#ifdef __linux__
    // Probe for Landlock support
    struct landlock_ruleset_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.handled_access_fs = LANDLOCK_ACCESS_FS_ALL;
    int fd = landlock_create_ruleset(&attr, sizeof(attr), 0);
    if (fd >= 0) {
        supported_ = true;
        close(fd);
    } else {
        supported_ = (errno != ENOSYS && errno != EOPNOTSUPP);
    }
#endif
}

std::string Sandbox::resolve_home_dir() const {
    const char* home = getenv("HOME");
    if (home && home[0] != '\0') {
        return std::string(home);
    }
    // Fallback
    return "/tmp";
}

bool Sandbox::ensure_directory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    // Create recursively
    // Find the parent
    size_t pos = path.rfind('/');
    if (pos != std::string::npos && pos > 0) {
        std::string parent = path.substr(0, pos);
        if (!ensure_directory(parent)) {
            return false;
        }
    }
    return mkdir(path.c_str(), 0700) == 0;
}

bool Sandbox::init() {
    std::string home = resolve_home_dir();
    base_dir_ = home + "/.opencrank";
    db_dir_ = base_dir_ + "/db";
    jail_dir_ = base_dir_ + "/jail";

    // Create directory structure
    if (!ensure_directory(base_dir_)) {
        LOG_ERROR("[Sandbox] Failed to create base directory: %s (%s)",
                  base_dir_.c_str(), strerror(errno));
        return false;
    }
    if (!ensure_directory(db_dir_)) {
        LOG_ERROR("[Sandbox] Failed to create db directory: %s (%s)",
                  db_dir_.c_str(), strerror(errno));
        return false;
    }
    if (!ensure_directory(jail_dir_)) {
        LOG_ERROR("[Sandbox] Failed to create jail directory: %s (%s)",
                  jail_dir_.c_str(), strerror(errno));
        return false;
    }

    // Also create jail sub-directories for memory files
    ensure_directory(jail_dir_ + "/memory");
    ensure_directory(base_dir_ + "/plugins"); // For plugins

    LOG_INFO("[Sandbox] Landlock supported: %s", supported_ ? "yes" : "no");
    LOG_INFO("[Sandbox] Directories initialized:");
    LOG_INFO("[Sandbox]   base: %s", base_dir_.c_str());
    LOG_INFO("[Sandbox]   db:   %s", db_dir_.c_str());
    LOG_INFO("[Sandbox]   jail: %s", jail_dir_.c_str());

    return true;
}

std::string Sandbox::memory_db_path() const {
    return db_dir_ + "/memory.db";
}

void Sandbox::allow_path(const std::string& path) {
    if (!active_) {
        extra_allowed_paths_.push_back(path);
    }
}

bool Sandbox::is_path_in_jail(const std::string& path) const {
    if (jail_dir_.empty()) return false;

    // Resolve to absolute path for comparison
    char resolved[PATH_MAX];
    const char* rp = realpath(path.c_str(), resolved);
    if (!rp) {
        // File may not exist yet; do prefix check on the raw path
        if (path.size() >= jail_dir_.size() &&
            path.compare(0, jail_dir_.size(), jail_dir_) == 0) {
            return (path.size() == jail_dir_.size() || path[jail_dir_.size()] == '/');
        }
        return false;
    }

    std::string abs_path(rp);
    if (abs_path.size() >= jail_dir_.size() &&
        abs_path.compare(0, jail_dir_.size(), jail_dir_) == 0) {
        return (abs_path.size() == jail_dir_.size() || abs_path[jail_dir_.size()] == '/');
    }
    return false;
}

bool Sandbox::is_path_allowed(const std::string& path) const {
    if (!active_) return true;  // Sandbox not active, everything allowed

    char resolved[PATH_MAX];
    const char* rp = realpath(path.c_str(), resolved);
    std::string check_path = rp ? std::string(rp) : path;

    // Check base_dir (includes db_dir and jail_dir)
    if (check_path.size() >= base_dir_.size() &&
        check_path.compare(0, base_dir_.size(), base_dir_) == 0) {
        return (check_path.size() == base_dir_.size() || check_path[base_dir_.size()] == '/');
    }

    // Check extra allowed paths
    for (size_t i = 0; i < extra_allowed_paths_.size(); ++i) {
        const std::string& allowed = extra_allowed_paths_[i];
        if (check_path.size() >= allowed.size() &&
            check_path.compare(0, allowed.size(), allowed) == 0) {
            return (check_path.size() == allowed.size() || check_path[allowed.size()] == '/');
        }
    }

    return false;
}

std::string Sandbox::resolve_in_jail(const std::string& relative_path) const {
    if (relative_path.empty() || relative_path == ".") {
        return jail_dir_;
    }
    if (relative_path[0] == '/') {
        return relative_path;  // Already absolute
    }
    return jail_dir_ + "/" + relative_path;
}

bool Sandbox::activate() {
    if (active_) return true;

#ifndef __linux__
    LOG_WARN("[Sandbox] Landlock is only available on Linux. Sandbox NOT active.");
    return false;
#else
    if (!supported_) {
        LOG_WARN("[Sandbox] Landlock not supported by this kernel. Sandbox NOT active.");
        LOG_WARN("[Sandbox] Upgrade to Linux >= 5.13 for filesystem sandboxing.");
        return false;
    }

    // 1. Create a Landlock ruleset handling all FS access types
    struct landlock_ruleset_attr ruleset_attr;
    memset(&ruleset_attr, 0, sizeof(ruleset_attr));
    ruleset_attr.handled_access_fs = LANDLOCK_ACCESS_FS_ALL;

    int ruleset_fd = landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
    if (ruleset_fd < 0) {
        LOG_ERROR("[Sandbox] Failed to create Landlock ruleset: %s", strerror(errno));
        return false;
    }

    // Helper lambda to add a read+write rule for a directory
    auto add_rw_rule = [&](const std::string& dir_path) -> bool {
        int dir_fd = open(dir_path.c_str(), O_PATH | O_CLOEXEC);
        if (dir_fd < 0) {
            LOG_ERROR("[Sandbox] Cannot open '%s' for Landlock rule: %s",
                      dir_path.c_str(), strerror(errno));
            return false;
        }

        struct landlock_path_beneath_attr path_attr;
        memset(&path_attr, 0, sizeof(path_attr));
        path_attr.allowed_access = LANDLOCK_ACCESS_FS_ALL;
        path_attr.parent_fd = dir_fd;

        int ret = landlock_add_rule(ruleset_fd,
                                     LANDLOCK_RULE_PATH_BENEATH,
                                     &path_attr, 0);
        close(dir_fd);

        if (ret < 0) {
            LOG_ERROR("[Sandbox] Failed to add Landlock rule for '%s': %s",
                      dir_path.c_str(), strerror(errno));
            return false;
        }
        return true;
    };

    // Helper for read-only access (e.g., system libraries, executables)
    auto add_ro_rule = [&](const std::string& dir_path) -> bool {
        int dir_fd = open(dir_path.c_str(), O_PATH | O_CLOEXEC);
        if (dir_fd < 0) {
            // Not critical if path doesn't exist
            return false;
        }

        struct landlock_path_beneath_attr path_attr;
        memset(&path_attr, 0, sizeof(path_attr));
        path_attr.allowed_access =
            LANDLOCK_ACCESS_FS_EXECUTE |
            LANDLOCK_ACCESS_FS_READ_FILE |
            LANDLOCK_ACCESS_FS_READ_DIR;
        path_attr.parent_fd = dir_fd;

        int ret = landlock_add_rule(ruleset_fd,
                                     LANDLOCK_RULE_PATH_BENEATH,
                                     &path_attr, 0);
        close(dir_fd);

        if (ret < 0) {
            return false;
        }
        return true;
    };

    // 2. Allow full read+write access to ~/.opencrank (base_dir_)
    //    This covers db/, jail/, and any config inside.
    if (!add_rw_rule(base_dir_)) {
        close(ruleset_fd);
        return false;
    }
    LOG_DEBUG("[Sandbox] Allowed R/W: %s", base_dir_.c_str());

    // 3. Allow read-only access to essential system directories so that
    //    the process can still run, load libraries, and execute basic tools.
    const char* readonly_dirs[] = {
        "/usr",
        "/lib",
        "/lib64",
        "/bin",
        "/sbin",
        "/etc",       // DNS resolution, SSL certs, timezone, etc.
        "/dev",       // /dev/null, /dev/urandom
        "/proc",      // /proc/self, etc.
        "/sys",       // Some tools need this
        "/tmp",       // Temporary files for tools
        "/run",       // systemd, dbus sockets
        NULL
    };

    // Allow plugins to read their files (but not modify)
    add_ro_rule(base_dir_ + "/plugins");

    for (int i = 0; readonly_dirs[i] != NULL; ++i) {
        if (add_ro_rule(readonly_dirs[i])) {
            LOG_DEBUG("[Sandbox] Allowed R/O: %s", readonly_dirs[i]);
        }
    }

    // 4. Allow R/W to /tmp so child processes can use temp files
    add_rw_rule("/tmp");

    // 5. Add extra allowed paths (R/W)
    for (size_t i = 0; i < extra_allowed_paths_.size(); ++i) {
        if (add_rw_rule(extra_allowed_paths_[i])) {
            LOG_DEBUG("[Sandbox] Allowed R/W (extra): %s", extra_allowed_paths_[i].c_str());
        }
    }

    // 6. Prevent gaining new privileges (required before restrict_self)
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        LOG_ERROR("[Sandbox] Failed to set no_new_privs: %s", strerror(errno));
        close(ruleset_fd);
        return false;
    }

    // 7. Enforce the ruleset on this process (and all future children)
    if (landlock_restrict_self(ruleset_fd, 0) < 0) {
        LOG_ERROR("[Sandbox] Failed to restrict self: %s", strerror(errno));
        close(ruleset_fd);
        return false;
    }

    close(ruleset_fd);
    active_ = true;

    LOG_INFO("[Sandbox] ===== LANDLOCK SANDBOX ACTIVE =====");
    LOG_INFO("[Sandbox] Process and all children restricted to:");
    LOG_INFO("[Sandbox]   R/W: %s", base_dir_.c_str());
    LOG_INFO("[Sandbox]   R/O: /usr, /lib, /bin, /etc, /dev, /proc, /sys");
    LOG_INFO("[Sandbox]   R/W: /tmp");
    LOG_INFO("[Sandbox] NO filesystem access outside these paths is possible.");

    return true;
#endif // __linux__
}

} // namespace opencrank
