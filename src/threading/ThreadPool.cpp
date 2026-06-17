#include "threading/ThreadPool.h"
#include "debug/Log.h"

ThreadPool::ThreadPool(size_t numThreads) {
    if (numThreads == 0) {
        numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 4; // fallback
    }

    for (size_t i = 0; i < numThreads; ++i) {
        m_workers.emplace_back([this] {
            while (true) {
                PrioritizedTask task;

                {
                    std::unique_lock lock(m_queueMutex);
                    m_condition.wait(lock, [this] {
                        return m_stop || !m_tasks.empty();
                    });

                    if (m_stop && m_tasks.empty()) return;

                    task = std::move(m_tasks.top());
                    m_tasks.pop();
                }

                try {
                    task.task();
                } catch (const std::exception& e) {
                    LOG_ERROR("ThreadPool worker task threw: " << e.what());
                } catch (...) {
                    LOG_ERROR("ThreadPool worker task threw unknown exception");
                }
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard lock(m_queueMutex);
        m_stop = true;
    }
    m_condition.notify_all();
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::enqueuePriority(std::function<void()> task, int priority) {
    // Wrap user task so exceptions are caught and logged instead of
    // propagating to the worker thread (which would call std::terminate).
    auto safeTask = [t = std::move(task)]() {
        try {
            t();
        } catch (const std::exception& e) {
            LOG_ERROR("enqueuePriority task threw: " << e.what());
        } catch (...) {
            LOG_ERROR("enqueuePriority task threw unknown exception");
        }
    };
    {
        std::lock_guard lock(m_queueMutex);
        m_tasks.push({priority, std::move(safeTask)});
    }
    m_condition.notify_one();
}

size_t ThreadPool::pendingCount() const {
    std::lock_guard lock(m_queueMutex);
    return m_tasks.size();
}
