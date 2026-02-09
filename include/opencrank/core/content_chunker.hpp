/*
 * opencrank C++11 - Content Chunker
 * 
 * Handles large content that exceeds token limits by splitting into
 * manageable chunks with search and navigation support.
 */
#ifndef opencrank_CORE_CONTENT_CHUNKER_HPP
#define opencrank_CORE_CONTENT_CHUNKER_HPP

#include <string>
#include <vector>
#include <map>

namespace opencrank {

// ============================================================================
// Content Chunker - Handles large content that exceeds token limits
// ============================================================================

struct ChunkedContent {
    std::string id;              // Unique identifier for this content
    std::string full_content;    // The complete content
    std::string source;          // Where this content came from (tool name, url, etc.)
    size_t chunk_size;           // Size of each chunk in characters
    size_t total_chunks;         // Total number of chunks
    
    ChunkedContent() : chunk_size(8000), total_chunks(0) {}
};

class ContentChunker {
public:
    ContentChunker();
    
    // Store content and return a unique ID
    // chunk_size: 0 means use default (8000)
    std::string store(const std::string& content, const std::string& source, size_t chunk_size = 0);
    
    // Get a specific chunk (0-indexed)
    std::string get_chunk(const std::string& id, size_t chunk_index) const;
    
    // Get summary info about stored content
    std::string get_info(const std::string& id) const;
    
    // Search within stored content
    std::string search(const std::string& id, const std::string& query, size_t context_chars = 500) const;
    
    // Check if content exists
    bool has(const std::string& id) const;
    
    // Clear stored content
    void clear();
    void remove(const std::string& id);
    
    // Get total chunks for an ID
    size_t get_total_chunks(const std::string& id) const;
    
private:
    std::map<std::string, ChunkedContent> storage_;
    int next_id_;
};

} // namespace opencrank

#endif // opencrank_CORE_CONTENT_CHUNKER_HPP
