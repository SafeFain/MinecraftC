#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Enqueue a task. Returns a future for the result.
    template<typename F>
    auto enqueue(F&& f) -> std::future<decltype(f())>;

    // Enqueue with priority (higher priority runs first)
    void enqueuePriority(std::function<void()> task, int priority = 0);

    // Number of pending tasks
    size_t pendingCount() const;

    // Number of worker threads
    size_t threadCount() const { return m_workers.size(); }

private:
    struct PrioritizedTask {
        int priority;
        std::function<void()> task;

        bool operator<(const PrioritizedTask& other) const {
            return priority < other.priority; // higher priority first
        }
    };

    std::vector<std::thread> m_workers;
    std::priority_queue<PrioritizedTask> m_tasks;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_condition;
    std::atomic<bool> m_stop{false};
};

// Template must be in header
template<typename F>
auto ThreadPool::enqueue(F&& f) -> std::future<decltype(f())> {
    using ReturnType = decltype(f());

    auto promise = std::make_shared<std::promise<ReturnType>>();
    auto future = promise->get_future();

    auto wrappedTask = [promise, task = std::forward<F>(f)]() mutable {
        try {
            promise->set_value(task());
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };

    {
        std::lock_guard lock(m_queueMutex);
        m_tasks.push({0, std::move(wrappedTask)});
    }
    m_condition.notify_one();

    return future;
}
