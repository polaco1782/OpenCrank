#include <opencrank/plugins/polls/polls.hpp>
#include <opencrank/core/loader.hpp>
#include <opencrank/core/utils.hpp>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace opencrank {

// ============ Poll methods ============

bool Poll::is_expired() const {
    if (expires_at == 0) return false;
    return current_timestamp() > expires_at;
}

bool Poll::is_active() const {
    return !is_closed && !is_expired();
}

int64_t Poll::time_remaining() const {
    if (expires_at == 0) return 0;
    int64_t now = current_timestamp();
    return expires_at > now ? expires_at - now : 0;
}

// ============ PollResults methods ============

double PollResults::get_percentage(size_t option_index) const {
    if (total_votes == 0 || option_index >= vote_counts.size()) return 0.0;
    return (vote_counts[option_index] * 100.0) / total_votes;
}

int PollResults::get_winning_option() const {
    if (vote_counts.empty() || total_votes == 0) return -1;
    
    int max_count = 0;
    int winner = -1;
    bool tie = false;
    
    for (size_t i = 0; i < vote_counts.size(); ++i) {
        if (vote_counts[i] > max_count) {
            max_count = vote_counts[i];
            winner = static_cast<int>(i);
            tie = false;
        } else if (vote_counts[i] == max_count && max_count > 0) {
            tie = true;
        }
    }
    
    return tie ? -1 : winner;
}

// ============ Normalization functions ============

Poll normalize_poll(const PollInput& input, const PollNormalizeOptions& options) {
    Poll poll;
    
    // Validate and set question
    poll.question = trim(input.question);
    if (poll.question.empty()) {
        throw PollError("Poll question is required");
    }
    
    // Validate and set options
    for (size_t i = 0; i < input.options.size(); ++i) {
        std::string opt = trim(input.options[i]);
        if (!opt.empty()) {
            poll.options.push_back(opt);
        }
    }
    
    if (poll.options.size() < 2) {
        throw PollError("Poll requires at least 2 options");
    }
    
    if (options.max_options > 0 && static_cast<int>(poll.options.size()) > options.max_options) {
        std::ostringstream oss;
        oss << "Poll supports at most " << options.max_options << " options";
        throw PollError(oss.str());
    }
    
    // Validate and set max selections
    poll.max_selections = input.max_selections > 0 ? input.max_selections : 1;
    
    if (poll.max_selections < 1) {
        throw PollError("maxSelections must be at least 1");
    }
    
    if (poll.max_selections > static_cast<int>(poll.options.size())) {
        throw PollError("maxSelections cannot exceed option count");
    }
    
    // Set duration
    poll.duration_hours = normalize_poll_duration(
        input.duration_hours, 
        options.default_hours, 
        options.max_hours
    );
    
    // Set timestamps
    poll.created_at = current_timestamp();
    if (poll.duration_hours > 0) {
        poll.expires_at = poll.created_at + (poll.duration_hours * 3600);
    }
    
    // Generate unique ID
    poll.id = generate_uuid();
    poll.is_closed = false;
    
    return poll;
}

int normalize_poll_duration(int duration_hours, int default_hours, int max_hours) {
    int base = duration_hours > 0 ? duration_hours : default_hours;
    return clamp(base, 1, max_hours);
}

std::string validate_vote(const Poll& poll, const std::vector<int>& selected_options) {
    if (!poll.is_active()) {
        return "Poll is no longer active";
    }
    
    if (selected_options.empty()) {
        return "At least one option must be selected";
    }
    
    if (static_cast<int>(selected_options.size()) > poll.max_selections) {
        std::ostringstream oss;
        oss << "Cannot select more than " << poll.max_selections << " option(s)";
        return oss.str();
    }
    
    // Check for duplicates and valid indices
    std::vector<bool> seen(poll.options.size(), false);
    for (size_t i = 0; i < selected_options.size(); ++i) {
        int opt = selected_options[i];
        if (opt < 0 || opt >= static_cast<int>(poll.options.size())) {
            return "Invalid option index";
        }
        if (seen[opt]) {
            return "Duplicate option selection";
        }
        seen[opt] = true;
    }
    
    return "";  // Valid
}

// ============ PollManager ============

PollManager& PollManager::instance() {
    static PollManager manager;
    return manager;
}

Poll& PollManager::create_poll(const PollInput& input, const PollNormalizeOptions& options) {
    Poll poll = normalize_poll(input, options);
    polls_[poll.id] = poll;
    votes_[poll.id] = std::vector<PollVote>();
    return polls_[poll.id];
}

Poll* PollManager::get_poll(const std::string& poll_id) {
    std::map<std::string, Poll>::iterator it = polls_.find(poll_id);
    return it != polls_.end() ? &it->second : NULL;
}

const Poll* PollManager::get_poll(const std::string& poll_id) const {
    std::map<std::string, Poll>::const_iterator it = polls_.find(poll_id);
    return it != polls_.end() ? &it->second : NULL;
}

bool PollManager::has_poll(const std::string& poll_id) const {
    return polls_.find(poll_id) != polls_.end();
}

bool PollManager::close_poll(const std::string& poll_id) {
    Poll* poll = get_poll(poll_id);
    if (!poll) return false;
    poll->is_closed = true;
    return true;
}

bool PollManager::delete_poll(const std::string& poll_id) {
    if (!has_poll(poll_id)) return false;
    polls_.erase(poll_id);
    votes_.erase(poll_id);
    return true;
}

std::string PollManager::vote(const std::string& poll_id,
                              const std::string& voter_id,
                              const std::vector<int>& selected_options) {
    Poll* poll = get_poll(poll_id);
    if (!poll) {
        return "Poll not found";
    }
    
    // Validate the vote
    std::string error = validate_vote(*poll, selected_options);
    if (!error.empty()) {
        return error;
    }
    
    // Check if already voted
    if (has_voted(poll_id, voter_id)) {
        return "You have already voted in this poll";
    }
    
    // Record the vote
    PollVote vote_record;
    vote_record.poll_id = poll_id;
    vote_record.voter_id = voter_id;
    vote_record.selected_options = selected_options;
    vote_record.voted_at = current_timestamp();
    
    votes_[poll_id].push_back(vote_record);
    
    return "";  // Success
}

PollResults PollManager::get_results(const std::string& poll_id) const {
    PollResults results;
    results.poll_id = poll_id;
    
    const Poll* poll = get_poll(poll_id);
    if (!poll) {
        return results;
    }
    
    // Initialize vote counts
    results.vote_counts.resize(poll->options.size(), 0);
    results.total_votes = 0;
    
    // Count votes
    std::map<std::string, std::vector<PollVote> >::const_iterator votes_it = votes_.find(poll_id);
    if (votes_it == votes_.end()) {
        return results;
    }
    
    const std::vector<PollVote>& poll_votes = votes_it->second;
    for (size_t i = 0; i < poll_votes.size(); ++i) {
        const PollVote& v = poll_votes[i];
        results.votes_by_voter[v.voter_id] = v.selected_options;
        results.total_votes++;
        
        for (size_t j = 0; j < v.selected_options.size(); ++j) {
            int opt = v.selected_options[j];
            if (opt >= 0 && opt < static_cast<int>(results.vote_counts.size())) {
                results.vote_counts[opt]++;
            }
        }
    }
    
    return results;
}

bool PollManager::has_voted(const std::string& poll_id, const std::string& voter_id) const {
    std::map<std::string, std::vector<PollVote> >::const_iterator it = votes_.find(poll_id);
    if (it == votes_.end()) return false;
    
    const std::vector<PollVote>& poll_votes = it->second;
    for (size_t i = 0; i < poll_votes.size(); ++i) {
        if (poll_votes[i].voter_id == voter_id) {
            return true;
        }
    }
    return false;
}

std::vector<int> PollManager::get_voter_selection(const std::string& poll_id, const std::string& voter_id) const {
    std::map<std::string, std::vector<PollVote> >::const_iterator it = votes_.find(poll_id);
    if (it == votes_.end()) return std::vector<int>();
    
    const std::vector<PollVote>& poll_votes = it->second;
    for (size_t i = 0; i < poll_votes.size(); ++i) {
        if (poll_votes[i].voter_id == voter_id) {
            return poll_votes[i].selected_options;
        }
    }
    return std::vector<int>();
}

size_t PollManager::cleanup_expired() {
    size_t removed = 0;
    std::vector<std::string> to_remove;
    
    for (std::map<std::string, Poll>::iterator it = polls_.begin(); it != polls_.end(); ++it) {
        if (it->second.is_expired()) {
            to_remove.push_back(it->first);
        }
    }
    
    for (size_t i = 0; i < to_remove.size(); ++i) {
        delete_poll(to_remove[i]);
        ++removed;
    }
    
    return removed;
}

std::vector<std::string> PollManager::active_poll_ids() const {
    std::vector<std::string> ids;
    for (std::map<std::string, Poll>::const_iterator it = polls_.begin(); it != polls_.end(); ++it) {
        if (it->second.is_active()) {
            ids.push_back(it->first);
        }
    }
    return ids;
}

// ============ Formatting functions ============

std::string format_poll(const Poll& poll) {
    std::ostringstream oss;
    
    oss << "📊 " << poll.question << "\n\n";
    
    for (size_t i = 0; i < poll.options.size(); ++i) {
        oss << (i + 1) << ". " << poll.options[i] << "\n";
    }
    
    if (poll.max_selections > 1) {
        oss << "\n(Select up to " << poll.max_selections << " options)";
    }
    
    if (poll.duration_hours > 0) {
        int64_t remaining = poll.time_remaining();
        if (remaining > 0) {
            int hours = static_cast<int>(remaining / 3600);
            int minutes = static_cast<int>((remaining % 3600) / 60);
            oss << "\n⏱ ";
            if (hours > 0) {
                oss << hours << "h ";
            }
            oss << minutes << "m remaining";
        }
    }
    
    return oss.str();
}

std::string format_poll_results(const Poll& poll, const PollResults& results) {
    std::ostringstream oss;
    
    oss << "📊 " << poll.question << "\n\n";
    
    // Find max count for bar scaling
    int max_count = 0;
    for (size_t i = 0; i < results.vote_counts.size(); ++i) {
        if (results.vote_counts[i] > max_count) {
            max_count = results.vote_counts[i];
        }
    }
    
    // Display options with bars
    for (size_t i = 0; i < poll.options.size(); ++i) {
        double pct = results.get_percentage(i);
        int bar_len = max_count > 0 ? 
            static_cast<int>((results.vote_counts[i] * 10) / max_count) : 0;
        
        oss << poll.options[i] << "\n";
        
        // Progress bar
        for (int j = 0; j < bar_len; ++j) oss << "▓";
        for (int j = bar_len; j < 10; ++j) oss << "░";
        
        oss << " " << std::fixed << std::setprecision(1) << pct << "% (" 
            << results.vote_counts[i] << ")\n\n";
    }
    
    oss << "Total votes: " << results.total_votes;
    
    if (poll.is_closed) {
        oss << " (Closed)";
    } else if (poll.is_expired()) {
        oss << " (Expired)";
    }
    
    return oss.str();
}

// ============ PollsPlugin ============

PollsPlugin::PollsPlugin() {}

const char* PollsPlugin::name() const { return "polls"; }
const char* PollsPlugin::version() const { return "1.0.0"; }
const char* PollsPlugin::description() const { return "Poll management service"; }

bool PollsPlugin::init(const Config& /*cfg*/) {
    initialized_ = true;
    return true;
}

void PollsPlugin::shutdown() {
    initialized_ = false;
}

} // namespace opencrank

// Export plugin for dynamic loading (only when building as shared library)
OPENCRANK_DECLARE_PLUGIN(opencrank::PollsPlugin, "polls", "1.0.0",
                        "Poll management service", "service")