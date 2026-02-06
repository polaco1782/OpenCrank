/*
 * OpenCrank C++11 - Thread Pool for Message Processing
 */
#ifndef OPENCRANK_CORE_THREAD_POOL_HPP
#define OPENCRANK_CORE_THREAD_POOL_HPP

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

namespace opencrank {

// Simple thread pool for processing messages asynchronously
class ThreadPool {
public:
    ThreadPool(size_t num_threads = 4);
    ~ThreadPool();
    
    // Add a task to the queue
    void enqueue(std::function<void()> task);
    
    // Get number of threads
    size_t size() const { return threads_.size(); }
    
    // Get number of pending tasks
    size_t pending() const;
    
    // Shutdown the pool
    void shutdown();

private:
    void worker();
    
    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> tasks_;
    
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;
};

} // namespace opencrank

#endif // OPENCRANK_CORE_THREAD_POOL_HPP
