/*
 * OpenCrank C++11 - Message Handler
 * 
 * Handles incoming messages: routing to commands, skills, and AI.
 * Decoupled from the Application class for better modularity.
 */
#ifndef OPENCRANK_CORE_MESSAGE_HANDLER_HPP
#define OPENCRANK_CORE_MESSAGE_HANDLER_HPP

#include "types.hpp"
#include <string>

namespace opencrank {

// Forward declarations
class Session;

// ============================================================================
// Message Handler Functions
// ============================================================================

/**
 * Process an incoming message.
 * 
 * Routes the message to:
 * - Built-in commands (starting with /)
 * - Skill commands (matched skill names)
 * - AI for natural conversation
 * 
 * This is called from the thread pool after rate limiting.
 */
void process_message(const Message& msg);

/**
 * Main message callback for channels.
 * 
 * Performs deduplication, rate limiting, and enqueues
 * the message for processing in the thread pool.
 */
void on_message(const Message& msg);

/**
 * Error callback for channels.
 */
void on_error(const std::string& channel, const std::string& error);

/**
 * Notify plugins about an outgoing message.
 * 
 * Creates a pseudo-message and broadcasts to all plugins
 * (useful for gateway, logging, etc.)
 */
void notify_outgoing_message(
    const std::string& channel_id, 
    const std::string& to,
    const std::string& text, 
    const std::string& reply_to = ""
);

// ============================================================================
// Internal Message Processing
// ============================================================================

namespace detail {

/**
 * Handle a slash command (built-in or skill).
 * Returns the response to send, or empty string if command not found.
 */
std::string handle_command(
    const Message& msg,
    Session& session,
    const std::string& cmd_text
);

/**
 * Handle a skill command invocation.
 * Returns true if a skill was matched and handled.
 */
bool handle_skill_command(
    const Message& msg,
    Session& session,
    const std::string& cmd_text,
    std::string& response_out
);

/**
 * Handle a regular (non-command) message via AI.
 */
std::string handle_ai_message(
    const Message& msg,
    Session& session
);

/**
 * Send a response, optionally splitting into chunks.
 */
void send_response(
    const Message& original_msg,
    const std::string& response
);

} // namespace detail

} // namespace opencrank

#endif // OPENCRANK_CORE_MESSAGE_HANDLER_HPP
