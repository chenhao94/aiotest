#pragma once

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
        auto busy()
        {
            return todo.load(std::memory_order_relaxed);
        }

        // Switch wait/ready queue and pull at most one pending task.
        void roll()
        {
            ready.swap(wait);
            Task* task = nullptr;
            if (pending.pop(task))
            {
                ready.emplace_back(*task);
                delete task;
                task = nullptr;
            }
        }

        // Clear and setup current queue for next phase.
        void setup(std::vector<Task>& queue)
        {
            if (current)
            {
                current->clear();
                if (!(rand() & 255))
                    current->shrink_to_fit();
            }
            remain.store((current = &queue)->size(), std::memory_order_relaxed);
        }

        // Setup "Ready" phase.
        void setupReady()
        {
            todo.fetch_add(ready.size(), std::memory_order_relaxed);
            setup(ready);
        }

        // Setup "Done" phase.
        void setupDone()
        {
            setup(done);
        }

        // Clear todo counter;
        void clearTodo()
        {
            todo.store(0, std::memory_order_relaxed);
        }

        // Pop and execute one task from the current queue.
        auto operator ()()
        {
            auto i = remain.fetch_sub(1, std::memory_order_relaxed);
            if (i)
                (*current)[i - 1]();
            return i;
        }

        // Push a task into "wait" queue.
        void pushWait(const Task& task)
        {
            wait.emplace_back(task);
        }

        // Push a task into "done" queue.
        void pushDone(const Task& task)
        {
            done.emplace_back(task);
        }

        // Push a task into "pending" queue.
        void pushPending(const Task& task)
        {
            pending.push(new Task(task));
        }
    };
}
