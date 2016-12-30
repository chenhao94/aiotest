#pragma once

#include <iostream>
#include <algorithm>
#include <atomic>
#include <thread>
#include <functional>
#include <vector>
#include <chrono>
#include <random>

#include <boost/lockfree/queue.hpp>

#include "BTreeNodeBase.hpp"

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
        boost::lockfree::queue<Task*> pending;

        // Task LIFO queue.
        std::vector<Task> wait;
        std::vector<Task> ready;
        std::vector<Task> done;
        // LIFO head.
        std::atomic<ssize_t> remain = { 0 };

        // Reference the queue for current phase.
        std::vector<Task>* current = nullptr;

    public:
        TLQ() :
            rand(std::chrono::high_resolution_clock::now().time_since_epoch().count()),
            pending(std::thread::hardware_concurrency()),
            wait(),
            ready(),
            done()
        {
        }

        // Check if there's anything to do.
        size_t busy();

        // Pull and clear todo counter;
        size_t popTodo();

        // Switch wait/ready queue and pull at most one pending task.
        void roll();

        // Clear current queue.
        void clearCurrent(const char* reason = nullptr);

        // Setup current queue for next phase.
        void setup(std::vector<Task>& queue)
        {
            remain.store((current = &queue)->size(), std::memory_order_relaxed);
        }

        // Setup "Done" phase.
        void setupDone()
        {
            setup(done);
        }

        // Pop and execute one task from the current queue.
        auto operator ()()
        {
            auto i = remain.fetch_sub(1, std::memory_order_relaxed);
            if (i > 0)
                (*current)[i - 1]();
            return i > 0;
        }

        // Push a task into "wait" queue.
        void pushWait(Task task)
        {
            wait.emplace_back(task);
        }

        // Push a task into "done" queue.
        void pushDone(Task task)
        {
            done.emplace_back(task);
        }

        // Push a task into "pending" queue.
        void pushPending(Task task)
        {
            pending.push(new Task(task));
        }
    };
}
