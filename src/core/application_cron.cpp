#include <opencrank/core/application.hpp>
#include <opencrank/core/cron.hpp>
#include <opencrank/core/logger.hpp>
#include <opencrank/core/memory_tool.hpp>
#include <opencrank/core/utils.hpp>
#include <thread>
#include <ctime>

namespace opencrank {

void Application::start_cron_thread() {
    if (cron_thread_) return;
    cron_thread_stop_ = false;
    cron_thread_ = new std::thread([this] { cron_thread_func(); });
    LOG_INFO("Started CRON task thread");
}

void Application::stop_cron_thread() {
    if (!cron_thread_) return;
    cron_thread_stop_ = true;
    if (cron_thread_->joinable()) cron_thread_->join();
    delete cron_thread_;
    cron_thread_ = nullptr;
    LOG_INFO("Stopped CRON task thread");
}

void Application::cron_thread_func() {
    while (!cron_thread_stop_) {
        // Get the memory tool provider which manages tasks
        auto* tool_plugin = registry().get_tool("memory");
        auto* memtool = dynamic_cast<MemoryTool*>(tool_plugin);
        if (!memtool) {
            sleep_ms(60 * 1000); // No memory tool, wait 1 min
            continue;
        }
        auto tasks = memtool->manager().list_tasks(false, "");
        time_t now = time(nullptr);
        std::tm tm_now;
        localtime_r(&now, &tm_now);
        for (auto& task : tasks) {
            if (task.cron_expr.empty() || task.completed) continue;
            auto sched = CronSchedule::parse(task.cron_expr);
            if (sched.matches(tm_now)) {
                // Fire the task: mark as completed and log
                memtool->manager().complete_task(task.id);
                LOG_INFO("[CRON] Fired scheduled task: %s (id=%s)", task.content.c_str(), task.id.c_str());
                // Optionally: trigger agent action, notification, etc.
            }
        }
        sleep_ms(60 * 1000); // Check every minute
    }
}

} // namespace opencrank
