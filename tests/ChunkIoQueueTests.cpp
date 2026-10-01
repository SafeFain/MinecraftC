#include "world/ChunkStreamer.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    // Reuse stopped queue storage so a prematurely started worker can see
    // the previous lifetime's stop flag. Repeated starts exercise scheduling
    // variations without depending on the speed of terrain or filesystem I/O.
    std::optional<ChunkIoQueue> queue;
    std::atomic<int> completed{0};
    constexpr int lifetimes = 4096;
    for (int i = 0; i < lifetimes; ++i) {
        queue.emplace();
        queue->enqueue([&] { ++completed; });
        queue->drain();
        require(completed == i + 1, "each new queue runs its task before drain returns");
        queue->drain();
        queue.reset();
    }

    {
        ChunkIoQueue active;
        std::promise<void> entered;
        std::promise<void> release;
        auto released = release.get_future().share();
        active.enqueue([&] {
            entered.set_value();
            released.wait();
            ++completed;
        });
        entered.get_future().wait();
        auto drained = std::async(std::launch::async, [&] { active.drain(); });
        require(drained.wait_for(std::chrono::milliseconds(10)) ==
                    std::future_status::timeout,
                "drain waits for an active task after the pending queue is empty");
        release.set_value();
        drained.get();
        require(completed == lifetimes + 1, "drain observes active task completion");
    }

    std::vector<int> order;
    {
        ChunkIoQueue pending;
        pending.enqueue([] { throw std::runtime_error("recoverable cache failure"); });
        for (int i = 0; i < 32; ++i)
            pending.enqueue([&, i] { order.push_back(i); });
        // Destruction must finish pending tasks even after a task throws.
    }
    require(order.size() == 32, "destruction drains pending tasks after an exception");
    for (int i = 0; i < 32; ++i)
        require(order[static_cast<size_t>(i)] == i, "the I/O lane preserves task order");

    {
        ChunkIoQueue stopped;
        stopped.stop();
        stopped.stop();
        stopped.enqueue([&] { ++completed; });
        stopped.drain();
        require(completed == lifetimes + 1, "stopped queues reject new tasks");
    }
    std::cout << "Chunk I/O queue lifecycle tests passed\n";
}
