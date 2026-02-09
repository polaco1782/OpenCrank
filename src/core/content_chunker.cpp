/*
 * OpenCrank C++11 - Content Chunker Implementation
 * 
 * Handles storing, chunking, and searching large content.
 */
#include <opencrank/core/content_chunker.hpp>
#include <opencrank/core/logger.hpp>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace opencrank {

ContentChunker::ContentChunker() : next_id_(1) {}

std::string ContentChunker::store(const std::string& content, const std::string& source, size_t chunk_size) {
    // Use default if 0 is passed
    if (chunk_size == 0) chunk_size = 8000;
    
    ChunkedContent cc;
    cc.id = "chunk_" + std::to_string(next_id_++);
    cc.full_content = content;
    cc.source = source;
    cc.chunk_size = chunk_size;
    cc.total_chunks = (content.size() + chunk_size - 1) / chunk_size;
    
    storage_[cc.id] = cc;
    
    LOG_DEBUG("[ContentChunker] Stored content '%s' from '%s': %zu bytes, %zu chunks",
              cc.id.c_str(), source.c_str(), content.size(), cc.total_chunks);
    
    return cc.id;
}

std::string ContentChunker::get_chunk(const std::string& id, size_t chunk_index) const {
    std::map<std::string, ChunkedContent>::const_iterator it = storage_.find(id);
    if (it == storage_.end()) {
        return "Error: Content ID '" + id + "' not found.";
    }
    
    const ChunkedContent& cc = it->second;
    if (chunk_index >= cc.total_chunks) {
        return "Error: Chunk index " + std::to_string(chunk_index) + 
               " out of range. Total chunks: " + std::to_string(cc.total_chunks);
    }
    
    size_t start = chunk_index * cc.chunk_size;
    size_t len = std::min(cc.chunk_size, cc.full_content.size() - start);
    
    std::ostringstream oss;
    oss << "[Chunk " << (chunk_index + 1) << "/" << cc.total_chunks 
        << " from " << cc.source << "]\n";
    oss << cc.full_content.substr(start, len);
    
    if (chunk_index + 1 < cc.total_chunks) {
        oss << "\n\n[Use content_chunk tool with id=\"" << id 
            << "\" and chunk=" << (chunk_index + 1) << " for next chunk]";
    } else {
        oss << "\n\n[End of content]";
    }
    
    return oss.str();
}

std::string ContentChunker::get_info(const std::string& id) const {
    std::map<std::string, ChunkedContent>::const_iterator it = storage_.find(id);
    if (it == storage_.end()) {
        return "Content ID '" + id + "' not found.";
    }
    
    const ChunkedContent& cc = it->second;
    std::ostringstream oss;
    oss << "Content ID: " << cc.id << "\n";
    oss << "Source: " << cc.source << "\n";
    oss << "Total size: " << cc.full_content.size() << " characters\n";
    oss << "Total chunks: " << cc.total_chunks << " (each ~" << cc.chunk_size << " chars)\n";
    
    return oss.str();
}

std::string ContentChunker::search(const std::string& id, const std::string& query, size_t context_chars) const {
    std::map<std::string, ChunkedContent>::const_iterator it = storage_.find(id);
    if (it == storage_.end()) {
        return "Content ID '" + id + "' not found.";
    }
    
    const ChunkedContent& cc = it->second;
    std::string content_lower = cc.full_content;
    std::string query_lower = query;
    
    // Convert to lowercase for case-insensitive search
    for (size_t i = 0; i < content_lower.size(); ++i) {
        content_lower[i] = tolower(content_lower[i]);
    }
    for (size_t i = 0; i < query_lower.size(); ++i) {
        query_lower[i] = tolower(query_lower[i]);
    }
    
    std::vector<size_t> matches;
    size_t pos = 0;
    while ((pos = content_lower.find(query_lower, pos)) != std::string::npos) {
        matches.push_back(pos);
        pos += query_lower.size();
        if (matches.size() >= 10) break; // Limit matches
    }
    
    if (matches.empty()) {
        return "No matches found for '" + query + "' in content.";
    }
    
    std::ostringstream oss;
    oss << "Found " << matches.size() << " match(es) for '" << query << "':\n\n";
    
    for (size_t i = 0; i < matches.size(); ++i) {
        size_t match_pos = matches[i];
        size_t start = (match_pos > context_chars) ? (match_pos - context_chars) : 0;
        size_t end = std::min(match_pos + query.size() + context_chars, cc.full_content.size());
        
        oss << "--- Match " << (i + 1) << " (at position " << match_pos << ") ---\n";
        if (start > 0) oss << "...";
        oss << cc.full_content.substr(start, end - start);
        if (end < cc.full_content.size()) oss << "...";
        oss << "\n\n";
    }
    
    return oss.str();
}

bool ContentChunker::has(const std::string& id) const {
    return storage_.find(id) != storage_.end();
}

void ContentChunker::clear() {
    storage_.clear();
    LOG_DEBUG("[ContentChunker] Cleared all stored content");
}

void ContentChunker::remove(const std::string& id) {
    storage_.erase(id);
}

size_t ContentChunker::get_total_chunks(const std::string& id) const {
    std::map<std::string, ChunkedContent>::const_iterator it = storage_.find(id);
    if (it == storage_.end()) {
        return 0;
    }
    return it->second.total_chunks;
}

} // namespace opencrank
