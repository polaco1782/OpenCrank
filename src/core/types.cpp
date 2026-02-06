#include <opencrank/core/types.hpp>

namespace opencrank {

const char* channel_status_str(ChannelStatus s) {
    switch (s) {
        case ChannelStatus::STOPPED:  return "stopped";
        case ChannelStatus::STARTING: return "starting";
        case ChannelStatus::RUNNING:  return "running";
        case ChannelStatus::STOPPING: return "stopping";
        case ChannelStatus::ERROR:    return "error";
    }
    return "unknown";
}

} // namespace opencrank
