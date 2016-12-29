#pragma once

#include <atomic>
#include <thread>
#include <array>
#include <functional>

#include <boost/lockfree/queue.hpp>

#include "BTreeNodeBase.hpp"
#include "TLQ.hpp"
#include "State.hpp"
#include "Controller.hpp"

namespace tai
{
    class Controller;

    class Worker
    {
        using State = WorkerState;
    protected:
        // Controller of this worker.
        Controller& ctrl;

        // Local ID in the controller.
        const size_t id;

        // Global ID pool.
        static std::atomic<size_t> poolSize;
        static boost::lockfree::queue<size_t> pool;

        // Global ID.
        size_t gid;
        static thread_local size_t sgid;

        // Thread-local queue.
        static thread_local TLQ queue;
        // For referencing TLQ from other threads.
        TLQ* foreign;

        // Worker thread handle.
        std::unique_ptr<std::thread> handle = nullptr;

        // Neighbor list used for tree-style communication.
        std::array<size_t, 2> neighbor;

    public:
        using Task = std::function<void()>;

        std::atomic<State> state = { Pending };
        std::atomic<bool> reject = { false };

        Worker(Worker&& _);

        // Construct as the id-th worker of the controller.
        Worker(Controller& ctrl, size_t id);

        ~Worker();

        // Get global thread ID;
        auto getGID() const
        {
            return gid;
        }

        // Get global thread ID;
        // This can only be called directly from the worker thread.
        static auto getSGID()
        {
            return sgid;
        }

        // Get the thread-local object via global thread ID.
        template<typename T>
        static auto& getTL(T&& table)
        {
            if (sgid + 1 > table.size())
                table.resize(sgid + 1);
            return table[sgid];
        }

        // Push task into the queue specified.
        static void pushWait(Task task);
        static void pushDone(Task task);
        bool pushPending(Task task);

        // Broadcast a state to neighbors.
        void broadcast(State _, std::memory_order sync = std::memory_order_seq_cst);

    protected:
        // Decide whether this worker should close.
        bool closing();

        // Set worker state to "Sync", wait for all workers to sync and advance to next state.
        State barrier(State post);
        State barrier(std::function<State()> f);

        // Work stealing.
        void steal();

        // Main loop.
        void run();
    };
}
