#include <opencrank/ai/ai.hpp>

namespace opencrank {

std::string role_to_string(MessageRole role) {
    switch (role) {
        case MessageRole::SYSTEM: return "system";
        case MessageRole::USER: return "user";
        case MessageRole::ASSISTANT: return "assistant";
        default: return "user";
    }
}

MessageRole string_to_role(const std::string& str) {
    if (str == "system") return MessageRole::SYSTEM;
    if (str == "assistant") return MessageRole::ASSISTANT;
    return MessageRole::USER;
}

} // namespace opencrank
