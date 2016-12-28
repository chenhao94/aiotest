#pragma once

#include <algorithm>
#include <atomic>
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
        boost::lockfree::queue<Task*> pending;

        // Random engine.
        std::default_random_engine rand = std::default_random_engine(std::chrono::high_resolution_clock::now().time_since_epoch().count());

        // Task LIFO queue.
        std::vector<Task> wait;
        std::vector<Task> ready;
        std::vector<Task> done;
        // LIFO head.
        std::atomic<size_t> remain = { 0 };

        // Number of tasks to do in this round.
        std::atomic<size_t> todo = { 0 };

        // Reference the queue for current phase.
        std::vector<Task>* current = nullptr;

    public:
        // Check if there's anything to do.
        size_t busy()
        {
            return todo.load(std::memory_order_relaxed);
        }

        // Switch wait/ready queue and pull at most one pending task.
        void roll();

        // Setup next phase.
        void setup(std::vector<Task>& queue);
        void setupReady();
        void setupDone();

        // Pop and execute one task from the current queue.
        size_t operator ()();

        // Push a task into the queue specified.
        void pushWait(const Task& task);
        void pushDone(const Task& task);
        void pushPending(const Task& task);
    };
}
