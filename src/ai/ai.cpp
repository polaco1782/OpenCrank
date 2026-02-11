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

} // namespace opencrank
