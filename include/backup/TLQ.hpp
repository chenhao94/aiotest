#pragma once

#include <algorithm>
#include <atomic>
#include <thread>
#include <functional>
#include <vector>
#include <chrono>
#include <random>

#include <boost/lockfree/queue.hpp>

#include "Decl.hpp"
#include "Log.hpp"

namespace tai
{
    class TLQ
    {
    public:
        using Task = std::function<void()>;

    private:
        // Random engine.
        std::default_random_engine rand;

        // Pending queue.
        boost::lockfree::queue<Task*, boost::lockfree::fixed_sized<false>> pending;

        // Task LIFO queue.
        std::vector<Task> wait;
        std::vector<Task> ready;
        std::vector<Task> done;
        // LIFO head.
        std::atomic<ssize_t> remain = { 0 };

        // Reference the queue for current phase.
        std::vector<Task>* current = &ready;

    public:
        TAI_INLINE
        TLQ() :
            rand(std::chrono::high_resolution_clock::now().time_since_epoch().count()),
            pending(std::thread::hardware_concurrency()),
            wait(),
            ready(),
            done()
        {
            if (!pending.is_lock_free())
                Log::log("Warning: Pending queue is not lock-free.");
        }

        // Pull and clear todo counter;
        size_t popTodo();

        // Switch wait/ready queue and pull at most one pending task.
        void roll();

        // Clear current queue.
        void clearCurrent(const char* reason = nullptr);

        // Setup current queue for next phase.
        TAI_INLINE
        void setup(std::vector<Task>& queue)
        {
            remain.store((current = &queue)->size(), std::memory_order_relaxed);
        }

        // Setup "Done" phase.
        TAI_INLINE
        void setupDone()
        {
            setup(done);
        }

        // Pop and execute one task from the current queue.
        TAI_INLINE
        auto operator ()()
        {
            auto i = remain.fetch_sub(1, std::memory_order_relaxed);
            if (i > 0)
                (*current)[i - 1]();
            return i > 0;
        }

        // Push a task into "wait" queue.
        TAI_INLINE
        void pushWait(Task task)
        {
            wait.emplace_back(task);
        }

        // Push a task into "done" queue.
        TAI_INLINE
        void pushDone(Task task)
        {
            done.emplace_back(task);
        }

        // Push a task into "pending" queue.
        TAI_INLINE
        void pushPending(Task task)
        {
            // for (auto ptr = new Task(task); !pending.push(ptr); Log::log("Internal Error: Pending task fails. Retry..."));
            pending.push(new Task(task));
        }
    };
}
