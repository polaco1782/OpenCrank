/*
 * OpenCrank C++11 - Content Chunker Implementation
 * 
 * Handles storing, chunking, and searching large content.
 */
#include <opencrank/core/content_chunker.hpp>
#include <opencrank/core/utils.hpp>
#include <opencrank/core/logger.hpp>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <regex>

namespace opencrank {

// Shared match info used by search methods
namespace {
struct Match {
    size_t position;
    size_t length;
    size_t chunk_index;
};

// Find all matches in content, returns up to max_matches results
static std::vector<Match> find_matches(const std::string& content, const std::string& query,
                                       size_t chunk_size, bool use_regex, size_t max_matches = 20) {
    std::vector<Match> matches;
    
    if (use_regex) {
        try {
            std::regex pattern(query, std::regex::icase);
            std::sregex_iterator iter(content.begin(), content.end(), pattern);
            std::sregex_iterator end;
            
            while (iter != end && matches.size() < max_matches) {
                std::smatch m = *iter;
                Match match;
                match.position = static_cast<size_t>(m.position());
                match.length = static_cast<size_t>(m.length());
                match.chunk_index = match.position / chunk_size;
                matches.push_back(match);
                ++iter;
            }
        } catch (const std::regex_error&) {
            // Return empty on invalid regex; caller handles the error
        }
    } else {
        std::string content_lower = content;
        std::string query_lower = query;
        for (size_t i = 0; i < content_lower.size(); ++i)
            content_lower[i] = tolower(content_lower[i]);
        for (size_t i = 0; i < query_lower.size(); ++i)
            query_lower[i] = tolower(query_lower[i]);
        
        size_t pos = 0;
        while ((pos = content_lower.find(query_lower, pos)) != std::string::npos && matches.size() < max_matches) {
            Match match;
            match.position = pos;
            match.length = query.size();
            match.chunk_index = pos / chunk_size;
            matches.push_back(match);
            pos += query_lower.size();
        }
    }
    
    return matches;
}
} // anonymous namespace

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

std::string ContentChunker::get_chunk(const std::string& id, size_t chunk_index, bool clean_html) const {
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
    
    std::string chunk_content = cc.full_content.substr(start, len);
    
    // Apply HTML cleaning if requested
    if (clean_html) {
        chunk_content = strip_html_for_ai(chunk_content);
    }
    
    std::ostringstream oss;
    oss << "[Chunk " << (chunk_index + 1) << "/" << cc.total_chunks 
        << " from " << cc.source;
    if (clean_html) {
        oss << " (HTML cleaned)";
    }
    oss << "]\n";
    oss << chunk_content;
    
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

std::string ContentChunker::search_with_chunks(const std::string& id, const std::string& query, size_t context_chars, bool use_regex) const {
    std::map<std::string, ChunkedContent>::const_iterator it = storage_.find(id);
    if (it == storage_.end()) {
        return "Content ID '" + id + "' not found.";
    }
    
    const ChunkedContent& cc = it->second;
    
    // Use shared search helper
    std::vector<Match> matches = find_matches(cc.full_content, query, cc.chunk_size, use_regex);
    
    // Handle regex error (empty matches + regex mode might be a parse error)
    if (matches.empty() && use_regex) {
        // Validate the regex to distinguish "no matches" from "bad pattern"
        try {
            std::regex pattern(query, std::regex::icase);
            (void)pattern;
        } catch (const std::regex_error& e) {
            return "Invalid regex pattern: " + query + "\nError: " + e.what();
        }
    }
    
    if (matches.empty()) {
        return "No matches found for '" + query + "' in content." + 
               (use_regex ? std::string(" (regex mode)") : std::string());
    }
    
    // Group matches by chunk
    std::map<size_t, std::vector<size_t>> chunks_to_positions;
    for (size_t i = 0; i < matches.size(); ++i) {
        chunks_to_positions[matches[i].chunk_index].push_back(matches[i].position);
    }
    
    std::ostringstream oss;
    oss << "Found " << matches.size() << " match(es) for '" << query << "'" 
        << (use_regex ? " (regex)" : "") << " in " << chunks_to_positions.size() << " chunk(s):\n\n";
    
    // Display results grouped by chunk
    for (std::map<size_t, std::vector<size_t>>::const_iterator chunk_it = chunks_to_positions.begin();
         chunk_it != chunks_to_positions.end(); ++chunk_it) {
        
        size_t chunk_idx = chunk_it->first;
        const std::vector<size_t>& positions = chunk_it->second;
        
        oss << "=== Chunk " << chunk_idx << " (" << positions.size() << " match(es)) ===\n";
        oss << "To load this chunk, use: {\"tool\": \"content_chunk\", \"arguments\": {\"id\": \"" 
            << id << "\", \"chunk\": " << chunk_idx << "}}\n\n";
        
        // Show first 2 matches from this chunk as preview
        size_t preview_count = std::min(static_cast<size_t>(2), positions.size());
        for (size_t i = 0; i < preview_count; ++i) {
            size_t match_pos = positions[i];
            
            // Find the match length from our matches vector
            size_t match_len = query.size(); // default
            for (size_t j = 0; j < matches.size(); ++j) {
                if (matches[j].position == match_pos) {
                    match_len = matches[j].length;
                    break;
                }
            }
            
            size_t start = (match_pos > context_chars) ? (match_pos - context_chars) : 0;
            size_t end = std::min(match_pos + match_len + context_chars, cc.full_content.size());
            
            oss << "Match preview:\n";
            if (start > 0) oss << "...";
            oss << cc.full_content.substr(start, end - start);
            if (end < cc.full_content.size()) oss << "...";
            oss << "\n\n";
        }
        
        if (positions.size() > preview_count) {
            oss << "(" << (positions.size() - preview_count) << " more match(es) in this chunk)\n\n";
        }
    }
    
    return oss.str();
}

std::string ContentChunker::search_all_chunks(const std::string& query, size_t context_chars, bool use_regex) const {
    if (storage_.empty()) {
        return "No content is currently stored. All chunks have expired or been cleared.";
    }
    
    std::ostringstream oss;
    size_t total_matches = 0;
    size_t contents_with_matches = 0;
    
    oss << "Searching across " << storage_.size() << " stored content(s)" 
        << (use_regex ? " (regex mode)" : "") << ":\n\n";
    
    // Search each stored content
    for (std::map<std::string, ChunkedContent>::const_iterator it = storage_.begin();
         it != storage_.end(); ++it) {
        
        const std::string& content_id = it->first;
        const ChunkedContent& cc = it->second;
        
        // Use shared search helper
        std::vector<Match> matches = find_matches(cc.full_content, query, cc.chunk_size, use_regex);
        
        // If we found matches in this content, display them
        if (!matches.empty()) {
            contents_with_matches++;
            total_matches += matches.size();
            
            // Group matches by chunk
            std::map<size_t, std::vector<size_t>> chunks_to_positions;
            for (size_t i = 0; i < matches.size(); ++i) {
                chunks_to_positions[matches[i].chunk_index].push_back(matches[i].position);
            }
            
            oss << "## Content ID: " << content_id << " (" << cc.source << ")\n";
            oss << "Found " << matches.size() << " match(es) in " 
                << chunks_to_positions.size() << " chunk(s)\n\n";
            
            // Show results for first 3 chunks with matches
            size_t chunks_shown = 0;
            for (std::map<size_t, std::vector<size_t>>::const_iterator chunk_it = chunks_to_positions.begin();
                 chunk_it != chunks_to_positions.end() && chunks_shown < 3; ++chunk_it, ++chunks_shown) {
                
                size_t chunk_idx = chunk_it->first;
                const std::vector<size_t>& positions = chunk_it->second;
                
                oss << "  Chunk " << chunk_idx << " (" << positions.size() << " match(es))\n";
                oss << "  Load with: {\"tool\": \"content_chunk\", \"arguments\": {\"id\": \"" 
                    << content_id << "\", \"chunk\": " << chunk_idx << "}}\n";
                
                // Show first match from this chunk as preview
                if (!positions.empty()) {
                    size_t match_pos = positions[0];
                    
                    // Find the match length
                    size_t match_len = query.size();
                    for (size_t j = 0; j < matches.size(); ++j) {
                        if (matches[j].position == match_pos) {
                            match_len = matches[j].length;
                            break;
                        }
                    }
                    
                    size_t start = (match_pos > context_chars) ? (match_pos - context_chars) : 0;
                    size_t end = std::min(match_pos + match_len + context_chars, cc.full_content.size());
                    
                    oss << "  Preview: ";
                    if (start > 0) oss << "...";
                    oss << cc.full_content.substr(start, end - start);
                    if (end < cc.full_content.size()) oss << "...";
                    oss << "\n\n";
                }
            }
            
            if (chunks_to_positions.size() > chunks_shown) {
                oss << "  (" << (chunks_to_positions.size() - chunks_shown) 
                    << " more chunk(s) with matches in this content)\n\n";
            }
        }
    }
    
    if (contents_with_matches == 0) {
        return "No matches found for '" + query + "' in any stored content." +
               (use_regex ? " (regex mode)" : "");
    }
    
    oss << "\n=== Summary ===\n";
    oss << "Total: " << total_matches << " match(es) found across " 
        << contents_with_matches << " content(s)";
    
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
