/*
 * OpenCrank C++11 - Memory Manager Implementation
 */
#include <opencrank/memory/manager.hpp>
#include <fstream>
#include <sstream>
#include <ctime>
#include <cstdlib>
#include <iomanip>
#include <sys/stat.h>
#include <dirent.h>
#include <openssl/sha.h>

namespace opencrank {

MemoryManager::MemoryManager(const MemoryConfig& config)
    : config_(config)
    , store_(new MemoryStore())
    , initialized_(false)
{
}

MemoryManager::~MemoryManager() {
    shutdown();
}

bool MemoryManager::initialize() {
    if (initialized_) return true;
    
    // Ensure workspace directory exists
    struct stat st;
    if (stat(config_.workspace_dir.c_str(), &st) != 0) {
        set_error("Workspace directory does not exist: " + config_.workspace_dir);
        return false;
    }
    
    // Determine database path
    std::string db_path = config_.db_path;
    if (db_path.empty()) {
        db_path = config_.workspace_dir + "/.opencrank/memory.db";
    }
    
    // Ensure parent directory exists
    std::string db_dir = db_path.substr(0, db_path.rfind('/'));
    if (!db_dir.empty() && stat(db_dir.c_str(), &st) != 0) {
        // Create directory
        std::string cmd = "mkdir -p " + db_dir;
        system(cmd.c_str());
    }
    
    // Open database
    if (!store_->open(db_path)) {
        set_error("Failed to open database: " + store_->last_error());
        return false;
    }
    
    // Initialize schema
    if (!store_->ensure_schema()) {
        set_error("Failed to create schema: " + store_->last_error());
        return false;
    }
    
    initialized_ = true;
    return true;
}

void MemoryManager::shutdown() {
    if (store_) {
        store_->close();
    }
    initialized_ = false;
}

bool MemoryManager::is_initialized() const {
    return initialized_;
}

bool MemoryManager::sync() {
    if (!initialized_) {
        set_error("Memory manager not initialized");
        return false;
    }
    
    // Discover memory files
    std::vector<MemoryFile> files = discover_memory_files();
    std::vector<std::string> active_paths;
    
    for (const auto& file : files) {
        active_paths.push_back(file.path);
        
        // Check if file has changed
        MemoryFile existing;
        bool exists = store_->get_file(file.path, MemorySource::MEMORY, existing);
        
        if (exists && existing.hash == file.hash) {
            continue;  // File unchanged
        }
        
        // Index the file
        if (!index_file(file)) {
            // Log error but continue with other files
        }
    }
    
    // Remove stale files
    std::vector<std::string> stale = store_->get_stale_paths(active_paths, MemorySource::MEMORY);
    for (const auto& path : stale) {
        store_->delete_chunks_for_file(path, MemorySource::MEMORY);
        store_->delete_file(path, MemorySource::MEMORY);
    }
    
    return true;
}

bool MemoryManager::save_memory(const std::string& content, const std::string& filename) {
    std::string target = filename.empty() ? "MEMORY.md" : filename;
    std::string full_path = config_.workspace_dir + "/" + target;
    
    std::ofstream file(full_path.c_str());
    if (!file) {
        set_error("Failed to open file for writing: " + full_path);
        return false;
    }
    
    file << content;
    file.close();
    
    // Trigger sync to index the new content
    return sync();
}

bool MemoryManager::save_daily_memory(const std::string& content) {
    // Create memory directory if needed
    std::string memory_dir = config_.workspace_dir + "/memory";
    struct stat st;
    if (stat(memory_dir.c_str(), &st) != 0) {
        std::string cmd = "mkdir -p " + memory_dir;
        system(cmd.c_str());
    }
    
    // Get today's date
    std::string date = get_today_date();
    std::string filename = "memory/" + date + ".md";
    
    return save_memory(content, filename);
}

std::string MemoryManager::load_memory_file(const std::string& path) {
    std::string full_path = config_.workspace_dir + "/" + path;
    
    std::ifstream file(full_path.c_str());
    if (!file) {
        return "";
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool MemoryManager::append_to_memory(const std::string& content, const std::string& filename) {
    std::string full_path = config_.workspace_dir + "/" + filename;
    
    std::ofstream file(full_path.c_str(), std::ios::app);
    if (!file) {
        set_error("Failed to open file for appending: " + full_path);
        return false;
    }
    
    file << "\n" << content;
    file.close();
    
    return sync();
}

std::vector<std::string> MemoryManager::list_memory_files() {
    std::vector<std::string> result;
    std::vector<MemoryFile> files = store_->list_files(MemorySource::MEMORY);
    for (const auto& f : files) {
        result.push_back(f.path);
    }
    return result;
}

std::vector<MemorySearchResult> MemoryManager::search(const std::string& query) {
    return search(query, config_.search);
}

std::vector<MemorySearchResult> MemoryManager::search(const std::string& query, 
                                                       const MemorySearchConfig& config) {
    if (!initialized_) {
        return std::vector<MemorySearchResult>();
    }
    return store_->search(query, config);
}

std::string MemoryManager::get_memory_content(const std::string& path) {
    return load_memory_file(path);
}

// Task operations
std::string MemoryManager::create_task(const std::string& content, const std::string& context) {
    if (!initialized_) {
        set_error("Memory manager not initialized");
        return "";
    }
    
    MemoryTask task;
    task.id = generate_uuid();
    task.content = content;
    task.context = context;
    task.created_at = get_current_timestamp();
    task.due_at = 0;
    task.completed = false;
    task.completed_at = 0;
    
    if (store_->upsert_task(task)) {
        return task.id;
    }
    return "";
}

bool MemoryManager::complete_task(const std::string& task_id) {
    if (!initialized_) return false;
    return store_->complete_task(task_id);
}

std::vector<MemoryTask> MemoryManager::list_tasks(bool include_completed) {
    if (!initialized_) return std::vector<MemoryTask>();
    return store_->list_tasks(include_completed);
}

std::vector<MemoryTask> MemoryManager::get_pending_tasks() {
    if (!initialized_) return std::vector<MemoryTask>();
    return store_->get_pending_tasks();
}

std::vector<MemoryTask> MemoryManager::get_tasks_due_soon(int hours) {
    if (!initialized_) return std::vector<MemoryTask>();
    int64_t now = get_current_timestamp();
    int64_t deadline = now + (static_cast<int64_t>(hours) * 3600 * 1000);
    return store_->get_tasks_due_before(deadline);
}

bool MemoryManager::update_task_due(const std::string& task_id, int64_t due_at) {
    if (!initialized_) return false;
    
    MemoryTask task;
    if (!store_->get_task(task_id, task)) {
        return false;
    }
    
    task.due_at = due_at;
    return store_->upsert_task(task);
}

std::string MemoryManager::last_error() const {
    return last_error_;
}

const MemoryConfig& MemoryManager::config() const {
    return config_;
}

// Private methods

std::vector<MemoryFile> MemoryManager::discover_memory_files() {
    std::vector<MemoryFile> result;
    
    // Check for MEMORY.md (case-insensitive)
    std::vector<std::string> main_files = {"MEMORY.md", "memory.md"};
    for (const auto& name : main_files) {
        std::string path = config_.workspace_dir + "/" + name;
        struct stat st;
        if (stat(path.c_str(), &st) == 0) {
            result.push_back(build_file_entry(path));
            break;  // Only one main memory file
        }
    }
    
    // Check memory/ directory
    std::string memory_dir = config_.workspace_dir + "/memory";
    DIR* dir = opendir(memory_dir.c_str());
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            
            // Only .md files
            if (name.length() > 3 && name.substr(name.length() - 3) == ".md") {
                std::string path = memory_dir + "/" + name;
                result.push_back(build_file_entry(path));
            }
        }
        closedir(dir);
    }
    
    return result;
}

MemoryFile MemoryManager::build_file_entry(const std::string& abs_path) {
    MemoryFile file;
    file.abs_path = abs_path;
    file.source = MemorySource::MEMORY;
    
    // Compute relative path
    if (abs_path.find(config_.workspace_dir) == 0) {
        file.path = abs_path.substr(config_.workspace_dir.length() + 1);
    } else {
        file.path = abs_path;
    }
    
    // Get file stats
    struct stat st;
    if (stat(abs_path.c_str(), &st) == 0) {
        file.mtime = static_cast<int64_t>(st.st_mtime) * 1000;
        file.size = st.st_size;
    }
    
    // Compute hash of content
    std::ifstream f(abs_path.c_str());
    if (f) {
        std::stringstream buffer;
        buffer << f.rdbuf();
        file.hash = compute_hash(buffer.str());
    }
    
    return file;
}

bool MemoryManager::index_file(const MemoryFile& file) {
    // Read file content
    std::ifstream f(file.abs_path.c_str());
    if (!f) {
        return false;
    }
    
    std::stringstream buffer;
    buffer << f.rdbuf();
    std::string content = buffer.str();
    
    // Delete existing chunks
    store_->delete_chunks_for_file(file.path, file.source);
    
    // Create new chunks
    std::vector<MemoryChunk> chunks = chunk_content(content, file.path, file.source);
    
    for (const auto& chunk : chunks) {
        if (!store_->upsert_chunk(chunk)) {
            // Log error but continue
        }
    }
    
    // Update file record
    return store_->upsert_file(file);
}

std::vector<MemoryChunk> MemoryManager::chunk_content(const std::string& content,
                                                       const std::string& path,
                                                       MemorySource source) {
    std::vector<MemoryChunk> chunks;
    
    // Split content into paragraphs first
    std::vector<std::string> paragraphs = split_into_paragraphs(content);
    
    // Combine paragraphs into chunks of target size
    int max_chars = config_.chunking.max_chars();
    int overlap_chars = config_.chunking.overlap_chars();
    
    std::string current_chunk;
    int chunk_start_line = 1;
    int line_count = 1;
    
    for (size_t i = 0; i < paragraphs.size(); ++i) {
        const std::string& para = paragraphs[i];
        
        // Count lines in this paragraph
        int para_lines = 1;
        for (char c : para) {
            if (c == '\n') para_lines++;
        }
        
        // Check if adding this paragraph would exceed target size
        if (!current_chunk.empty() && 
            static_cast<int>(current_chunk.length() + para.length()) > max_chars) {
            // Save current chunk
            MemoryChunk chunk;
            chunk.id = generate_uuid();
            chunk.path = path;
            chunk.source = source;
            chunk.start_line = chunk_start_line;
            chunk.end_line = line_count - 1;
            chunk.text = current_chunk;
            chunk.hash = compute_hash(current_chunk);
            chunk.updated_at = get_current_timestamp();
            chunks.push_back(chunk);
            
            // Start new chunk with overlap
            if (overlap_chars > 0 && static_cast<int>(current_chunk.length()) > overlap_chars) {
                current_chunk = current_chunk.substr(current_chunk.length() - overlap_chars);
            } else {
                current_chunk.clear();
            }
            chunk_start_line = line_count;
        }
        
        if (!current_chunk.empty()) {
            current_chunk += "\n\n";
        }
        current_chunk += para;
        line_count += para_lines;
    }
    
    // Save final chunk if non-empty
    if (!current_chunk.empty()) {
        MemoryChunk chunk;
        chunk.id = generate_uuid();
        chunk.path = path;
        chunk.source = source;
        chunk.start_line = chunk_start_line;
        chunk.end_line = line_count;
        chunk.text = current_chunk;
        chunk.hash = compute_hash(current_chunk);
        chunk.updated_at = get_current_timestamp();
        chunks.push_back(chunk);
    }
    
    return chunks;
}

std::vector<std::string> MemoryManager::split_into_paragraphs(const std::string& content) {
    std::vector<std::string> paragraphs;
    std::string current;
    
    std::istringstream stream(content);
    std::string line;
    
    while (std::getline(stream, line)) {
        bool is_blank = line.empty() || (line.find_first_not_of(" \t\r") == std::string::npos);
        
        if (is_blank) {
            if (!current.empty()) {
                paragraphs.push_back(current);
                current.clear();
            }
        } else {
            if (!current.empty()) {
                current += "\n";
            }
            current += line;
        }
    }
    
    if (!current.empty()) {
        paragraphs.push_back(current);
    }
    
    return paragraphs;
}

std::string MemoryManager::compute_hash(const std::string& content) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(content.c_str()), 
           content.length(), hash);
    
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hash[i]);
    }
    return ss.str();
}

std::string MemoryManager::generate_uuid() {
    // Simple UUID v4 generation
    static bool seeded = false;
    if (!seeded) {
        srand(static_cast<unsigned>(time(nullptr)));
        seeded = true;
    }
    
    char buf[37];
    snprintf(buf, sizeof(buf), 
             "%04x%04x-%04x-%04x-%04x-%04x%04x%04x",
             rand() & 0xffff, rand() & 0xffff,
             rand() & 0xffff,
             (rand() & 0x0fff) | 0x4000,
             (rand() & 0x3fff) | 0x8000,
             rand() & 0xffff, rand() & 0xffff, rand() & 0xffff);
    
    return std::string(buf);
}

std::string MemoryManager::get_today_date() {
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    
    char buf[11];
    strftime(buf, sizeof(buf), "%Y-%m-%d", tm_info);
    return std::string(buf);
}

int64_t MemoryManager::get_current_timestamp() {
    return static_cast<int64_t>(time(nullptr)) * 1000;
}

void MemoryManager::set_error(const std::string& error) {
    last_error_ = error;
}

// Sync with reason and force flag
bool MemoryManager::sync(const std::string& reason, bool force) {
    // Log reason if needed in the future
    (void)reason;
    
    bool result = sync();
    
    if (result && should_sync_sessions(reason, force)) {
        result = sync_session_files();
    }
    
    return result;
}

// Session support methods

void MemoryManager::warm_session(const std::string& session_key) {
    if (!initialized_) {
        return;
    }
    
    // Mark session as warmed
    session_warm_.insert(session_key.empty() ? "_default_" : session_key);
    
    // Sync session files on warm
    sync_session_files();
}

bool MemoryManager::sync_session_files() {
    if (!initialized_) {
        set_error("Memory manager not initialized");
        return false;
    }
    
    std::vector<std::string> session_files = list_session_files();
    std::vector<std::string> active_paths;
    
    for (const auto& path : session_files) {
        SessionFileEntry entry = build_session_entry(path);
        active_paths.push_back(entry.path);
        
        // Check if file has changed
        MemoryFile existing;
        bool exists = store_->get_file(entry.path, MemorySource::SESSIONS, existing);
        
        // Compute hash of session file
        std::string content = load_file_content(entry.abs_path);
        std::string hash = compute_hash(content);
        
        if (exists && existing.hash == hash) {
            continue;  // File unchanged
        }
        
        // Index the session file
        if (!index_session_file(entry)) {
            // Log error but continue
        }
        
        // Remove from dirty set if it was there
        sessions_dirty_files_.erase(entry.path);
    }
    
    // Remove stale session files
    std::vector<std::string> stale = store_->get_stale_paths(active_paths, MemorySource::SESSIONS);
    for (const auto& path : stale) {
        store_->delete_chunks_for_file(path, MemorySource::SESSIONS);
        store_->delete_file(path, MemorySource::SESSIONS);
    }
    
    sessions_dirty_ = !sessions_dirty_files_.empty();
    return true;
}

bool MemoryManager::should_sync_sessions(const std::string& reason, bool force) {
    if (force) return true;
    if (sessions_dirty_) return true;
    if (reason == "session_end" || reason == "memory_search") return true;
    return false;
}

std::vector<std::string> MemoryManager::list_session_files() {
    std::vector<std::string> result;
    
    std::string session_dir = resolve_session_transcripts_dir();
    
    DIR* dir = opendir(session_dir.c_str());
    if (!dir) {
        return result;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        
        // Only .jsonl files (session transcripts)
        if (name.length() > 6 && name.substr(name.length() - 6) == ".jsonl") {
            result.push_back(session_dir + "/" + name);
        }
    }
    closedir(dir);
    
    return result;
}

SessionFileEntry MemoryManager::build_session_entry(const std::string& abs_path) {
    SessionFileEntry entry;
    entry.abs_path = abs_path;
    entry.path = session_path_for_file(abs_path);
    
    struct stat st;
    if (stat(abs_path.c_str(), &st) == 0) {
        entry.mtime_ms = static_cast<int64_t>(st.st_mtime) * 1000;
        entry.size = st.st_size;
    }
    
    return entry;
}

std::string MemoryManager::resolve_session_transcripts_dir() {
    // Default session directory is .opencrank/sessions
    return config_.workspace_dir + "/.opencrank/sessions";
}

std::string MemoryManager::session_path_for_file(const std::string& abs_path) {
    // Convert absolute path to workspace-relative path
    if (abs_path.find(config_.workspace_dir) == 0) {
        return abs_path.substr(config_.workspace_dir.length() + 1);
    }
    return abs_path;
}

bool MemoryManager::index_session_file(const SessionFileEntry& entry) {
    // Read file content
    std::string content = load_file_content(entry.abs_path);
    std::string hash = compute_hash(content);
    
    // Delete existing chunks
    store_->delete_chunks_for_file(entry.path, MemorySource::SESSIONS);
    
    // Extract text from session transcript
    std::string text = extract_session_text(content);
    text = normalize_session_text(text);
    
    if (!text.empty()) {
        // Create chunks from the extracted text
        std::vector<MemoryChunk> chunks = chunk_content(text, entry.path, MemorySource::SESSIONS);
        
        for (const auto& chunk : chunks) {
            if (!store_->upsert_chunk(chunk)) {
                // Log error but continue
            }
        }
    }
    
    // Update file record
    MemoryFile mf;
    mf.path = entry.path;
    mf.abs_path = entry.abs_path;
    mf.source = MemorySource::SESSIONS;
    mf.hash = hash;
    mf.mtime = entry.mtime_ms;
    mf.size = entry.size;
    
    return store_->upsert_file(mf);
}

std::string MemoryManager::extract_session_text(const std::string& content) {
    // Parse JSONL session transcript and extract user/assistant messages
    std::string result;
    std::istringstream stream(content);
    std::string line;
    
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        
        // Simple JSON parsing - look for role and content fields
        // Format: {"role":"user","content":"..."}
        std::string role;
        std::string msg_content;
        
        // Find role
        size_t role_pos = line.find("\"role\"");
        if (role_pos != std::string::npos) {
            size_t colon = line.find(':', role_pos);
            if (colon != std::string::npos) {
                size_t quote1 = line.find('"', colon);
                if (quote1 != std::string::npos) {
                    size_t quote2 = line.find('"', quote1 + 1);
                    if (quote2 != std::string::npos) {
                        role = line.substr(quote1 + 1, quote2 - quote1 - 1);
                    }
                }
            }
        }
        
        // Find content
        size_t content_pos = line.find("\"content\"");
        if (content_pos != std::string::npos) {
            size_t colon = line.find(':', content_pos);
            if (colon != std::string::npos) {
                size_t quote1 = line.find('"', colon);
                if (quote1 != std::string::npos) {
                    // Handle escaped quotes in content
                    size_t pos = quote1 + 1;
                    while (pos < line.length()) {
                        if (line[pos] == '"' && (pos == 0 || line[pos-1] != '\\')) {
                            break;
                        }
                        pos++;
                    }
                    msg_content = line.substr(quote1 + 1, pos - quote1 - 1);
                    // Unescape basic sequences
                    msg_content = unescape_json_string(msg_content);
                }
            }
        }
        
        // Only include user and assistant messages
        if ((role == "user" || role == "assistant") && !msg_content.empty()) {
            if (!result.empty()) {
                result += "\n\n";
            }
            result += "[" + role + "]: " + msg_content;
        }
    }
    
    return result;
}

std::string MemoryManager::normalize_session_text(const std::string& text) {
    // Normalize whitespace and remove excessive blank lines
    std::string result;
    result.reserve(text.length());
    
    bool prev_newline = false;
    bool prev_blank = false;
    
    for (size_t i = 0; i < text.length(); ++i) {
        char c = text[i];
        
        if (c == '\n') {
            if (prev_blank) {
                continue;  // Skip extra blank lines
            }
            if (prev_newline) {
                prev_blank = true;
            }
            prev_newline = true;
            result += c;
        } else if (c == '\r') {
            continue;  // Skip CR
        } else {
            prev_newline = false;
            prev_blank = false;
            result += c;
        }
    }
    
    return result;
}

std::string MemoryManager::unescape_json_string(const std::string& s) {
    std::string result;
    result.reserve(s.length());
    
    for (size_t i = 0; i < s.length(); ++i) {
        if (s[i] == '\\' && i + 1 < s.length()) {
            char next = s[i + 1];
            switch (next) {
                case 'n':  result += '\n'; ++i; break;
                case 't':  result += '\t'; ++i; break;
                case 'r':  result += '\r'; ++i; break;
                case '"':  result += '"';  ++i; break;
                case '\\': result += '\\'; ++i; break;
                default:   result += s[i]; break;
            }
        } else {
            result += s[i];
        }
    }
    
    return result;
}

std::string MemoryManager::load_file_content(const std::string& abs_path) {
    std::ifstream f(abs_path.c_str());
    if (!f) {
        return "";
    }
    std::stringstream buffer;
    buffer << f.rdbuf();
    return buffer.str();
}

bool MemoryManager::is_memory_path(const std::string& rel_path) {
    // Check if path is a memory file (MEMORY.md or memory/*.md)
    if (rel_path == "MEMORY.md" || rel_path == "memory.md") {
        return true;
    }
    if (rel_path.find("memory/") == 0 && rel_path.length() > 10 &&
        rel_path.substr(rel_path.length() - 3) == ".md") {
        return true;
    }
    return false;
}

bool MemoryManager::is_allowed_path(const std::string& abs_path) {
    // Check if path is within workspace
    return abs_path.find(config_.workspace_dir) == 0;
}

MemoryManager::MemoryStatus MemoryManager::status() const {
    MemoryStatus s;
    s.backend = "builtin";
    s.provider = "bm25";
    s.dirty = dirty_;
    s.sessions_dirty = sessions_dirty_;
    s.workspace_dir = config_.workspace_dir;
    s.db_path = config_.db_path;
    if (s.db_path.empty()) {
        s.db_path = config_.workspace_dir + "/.opencrank/memory.db";
    }
    
    if (initialized_ && store_) {
        s.files = static_cast<int>(store_->list_files(MemorySource::MEMORY).size());
        s.chunks = 0;  // Would need store method to count
        
        // Add source names
        s.sources.push_back("memory");
        if (!session_warm_.empty()) {
            s.sources.push_back("session");
        }
    } else {
        s.files = 0;
        s.chunks = 0;
    }
    
    return s;
}

// Search with session key
std::vector<MemorySearchResult> MemoryManager::search(const std::string& query,
                                                       const MemorySearchConfig& config,
                                                       const std::string& session_key) {
    std::vector<MemorySearchResult> results = search(query, config);
    
    // Optionally decorate with citations based on session context
    bool include_citations = should_include_citations(MemoryCitationMode::AUTO, session_key);
    decorate_citations(results, include_citations);
    
    return results;
}

MemoryManager::ReadFileResult MemoryManager::read_file(const std::string& rel_path, 
                                                        int from_line, 
                                                        int num_lines) {
    ReadFileResult result;
    result.path = rel_path;
    result.success = false;
    
    // Resolve path (relative to workspace)
    std::string abs_path = rel_path;
    if (rel_path[0] != '/') {
        abs_path = config_.workspace_dir + "/" + rel_path;
    }
    
    // Check if allowed
    if (!is_allowed_path(abs_path)) {
        result.error = "Path not allowed: " + rel_path;
        return result;
    }
    
    std::ifstream f(abs_path.c_str());
    if (!f) {
        result.error = "File not found: " + rel_path;
        return result;
    }
    
    // Read file lines
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) {
        lines.push_back(line);
    }
    
    int total_lines = static_cast<int>(lines.size());
    
    // Adjust line range (from_line is 0-indexed)
    int start = (from_line > 0) ? from_line : 0;
    int end = (num_lines > 0) ? (start + num_lines) : total_lines;
    
    if (start >= total_lines) {
        result.error = "Start line out of range";
        return result;
    }
    
    if (end > total_lines) {
        end = total_lines;
    }
    
    // Extract requested lines
    std::stringstream ss;
    for (int i = start; i < end; ++i) {
        if (i > start) ss << "\n";
        ss << lines[i];
    }
    
    result.text = ss.str();
    result.success = true;
    
    return result;
}

bool MemoryManager::should_include_citations(MemoryCitationMode mode, const std::string& session_key) {
    if (mode == MemoryCitationMode::OFF) return false;
    if (mode == MemoryCitationMode::ON) return true;
    
    // AUTO mode: include citations for certain chat types
    std::string chat_type = derive_chat_type_from_session_key(session_key);
    return (chat_type == "vscode" || chat_type == "coding");
}

void MemoryManager::decorate_citations(std::vector<MemorySearchResult>& results, bool include) {
    for (auto& r : results) {
        if (include && r.start_line > 0) {
            std::stringstream ss;
            ss << r.path << "#L" << r.start_line;
            if (r.end_line > r.start_line) {
                ss << "-L" << r.end_line;
            }
            r.citation = ss.str();
        }
    }
}

std::string MemoryManager::derive_chat_type_from_session_key(const std::string& session_key) {
    // Derive chat type from session key pattern
    // e.g., "vscode_12345" -> "vscode", "telegram_abc" -> "telegram"
    size_t underscore = session_key.find('_');
    if (underscore != std::string::npos) {
        return session_key.substr(0, underscore);
    }
    return session_key;
}

} // namespace opencrank
