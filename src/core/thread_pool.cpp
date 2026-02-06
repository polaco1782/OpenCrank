/*
 * OpenCrank C++11 - Thread Pool Implementation
 */
#include <opencrank/core/thread_pool.hpp>
#include <opencrank/core/logger.hpp>

namespace opencrank {

ThreadPool::ThreadPool(size_t num_threads) : stop_(false) {
    for (size_t i = 0; i < num_threads; ++i) {
        threads_.emplace_back([this] { worker(); });
    }
    LOG_INFO("Thread pool started with %zu workers", num_threads);
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (stop_) {
            LOG_WARN("Cannot enqueue task - thread pool is stopped");
            return;
        }
        tasks_.push(task);
    }
    condition_.notify_one();
}

size_t ThreadPool::pending() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return tasks_.size();
}

void ThreadPool::shutdown() {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (stop_) return;  // Already stopped
        stop_ = true;
    }
    
    condition_.notify_all();
    
    for (std::thread& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    LOG_INFO("Thread pool shutdown complete");
}

void ThreadPool::worker() {
    while (true) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(mutex_);
            
            // Wait for a task or stop signal
            condition_.wait(lock, [this] { 
                return stop_ || !tasks_.empty(); 
            });
            
            if (stop_ && tasks_.empty()) {
                return;  // Exit thread
            }
            
            if (!tasks_.empty()) {
                task = tasks_.front();
                tasks_.pop();
            }
        }
        
        // Execute task outside the lock
        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                LOG_ERROR("Thread pool task threw exception: %s", e.what());
            } catch (...) {
                LOG_ERROR("Thread pool task threw unknown exception");
            }
        }
    }
}

} // namespace opencrank
