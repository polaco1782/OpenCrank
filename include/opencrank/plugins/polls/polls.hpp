#ifndef opencrank_POLLS_POLLS_HPP
#define opencrank_POLLS_POLLS_HPP

#include <opencrank/core/plugin.hpp>
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <stdexcept>

namespace opencrank {

// Exception for poll validation errors
class PollError : public std::runtime_error {
public:
    explicit PollError(const std::string& msg) : std::runtime_error(msg) {}
};

// Poll input (raw, before normalization)
struct PollInput {
    std::string question;
    std::vector<std::string> options;
    int max_selections;      // 0 = not specified (default to 1)
    int duration_hours;      // 0 = not specified (use default)
    
    PollInput() : max_selections(0), duration_hours(0) {}
};

// Normalized poll (validated and ready to use)
struct Poll {
    std::string id;
    std::string question;
    std::vector<std::string> options;
    int max_selections;
    int duration_hours;
    int64_t created_at;      // Unix timestamp
    int64_t expires_at;      // Unix timestamp (0 = no expiry)
    bool is_closed;
    
    Poll() 
        : max_selections(1)
        , duration_hours(0)
        , created_at(0)
        , expires_at(0)
        , is_closed(false) {}
    
    // Check if poll has expired
    bool is_expired() const;
    
    // Check if poll is still active (not closed and not expired)
    bool is_active() const;
    
    // Get time remaining in seconds (0 if expired)
    int64_t time_remaining() const;
};

// Poll vote
struct PollVote {
    std::string poll_id;
    std::string voter_id;
    std::vector<int> selected_options;  // Indices into poll.options
    int64_t voted_at;
    
    PollVote() : voted_at(0) {}
};

// Poll results
struct PollResults {
    std::string poll_id;
    std::vector<int> vote_counts;  // Count per option
    int total_votes;
    std::map<std::string, std::vector<int> > votes_by_voter;  // voter_id -> selected options
    
    PollResults() : total_votes(0) {}
    
    // Get percentage for an option (0-100)
    double get_percentage(size_t option_index) const;
    
    // Get the winning option index (-1 if tie or no votes)
    int get_winning_option() const;
};

// Normalization options
struct PollNormalizeOptions {
    int max_options;        // Maximum number of options allowed
    int default_hours;      // Default duration in hours
    int max_hours;          // Maximum duration in hours
    
    PollNormalizeOptions() 
        : max_options(10)
        , default_hours(24)
        , max_hours(168) {}  // 1 week
};

// Normalize and validate poll input
// Throws PollError if validation fails
Poll normalize_poll(const PollInput& input, const PollNormalizeOptions& options = PollNormalizeOptions());

// Normalize duration hours within bounds
int normalize_poll_duration(int duration_hours, int default_hours, int max_hours);

// Validate a vote against a poll
// Returns empty string if valid, error message otherwise
std::string validate_vote(const Poll& poll, const std::vector<int>& selected_options);

// Poll manager - manages all polls in memory
class PollManager {
public:
    static PollManager& instance();
    
    // Create a new poll
    Poll& create_poll(const PollInput& input, const PollNormalizeOptions& options = PollNormalizeOptions());
    
    // Get poll by ID
    Poll* get_poll(const std::string& poll_id);
    const Poll* get_poll(const std::string& poll_id) const;
    
    // Check if poll exists
    bool has_poll(const std::string& poll_id) const;
    
    // Close a poll (stop accepting votes)
    bool close_poll(const std::string& poll_id);
    
    // Delete a poll
    bool delete_poll(const std::string& poll_id);
    
    // Cast a vote
    // Returns empty string on success, error message on failure
    std::string vote(const std::string& poll_id, 
                     const std::string& voter_id,
                     const std::vector<int>& selected_options);
    
    // Get results for a poll
    PollResults get_results(const std::string& poll_id) const;
    
    // Check if voter has already voted
    bool has_voted(const std::string& poll_id, const std::string& voter_id) const;
    
    // Get voter's vote (returns empty vector if not voted)
    std::vector<int> get_voter_selection(const std::string& poll_id, const std::string& voter_id) const;
    
    // Clean up expired polls
    size_t cleanup_expired();
    
    // Get all active poll IDs
    std::vector<std::string> active_poll_ids() const;
    
    // Poll count
    size_t poll_count() const { return polls_.size(); }

private:
    PollManager() {}
    PollManager(const PollManager&);
    PollManager& operator=(const PollManager&);
    
    std::map<std::string, Poll> polls_;
    std::map<std::string, std::vector<PollVote> > votes_;  // poll_id -> votes
};

// Polls plugin class
class PollsPlugin : public Plugin {
public:
    PollsPlugin();
    
    const char* name() const override;
    const char* version() const override;
    const char* description() const override;
    
    bool init(const Config& cfg) override;
    void shutdown() override;
};

} // namespace opencrank

#endif // opencrank_POLLS_POLLS_HPP
