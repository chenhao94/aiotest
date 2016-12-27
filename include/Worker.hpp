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
        Controller& ctrl;

        const size_t id;

        static std::atomic<size_t> poolSize;
        static boost::lockfree::queue<size_t> pool;
        size_t gid;
        static thread_local size_t sgid;

        static thread_local TLQ queue;
        TLQ* foreign;

        std::unique_ptr<std::thread> handle = nullptr;

        std::array<size_t, 2> neighbor;

    public:
        using Task = std::function<void()>;

        std::atomic<State> state = {Pending};
        std::atomic_flag block = ATOMIC_FLAG_INIT;

        Worker(Worker&& _);

        Worker(Controller& ctrl, const size_t& id);

        ~Worker();

        auto getGID() const
        {
            return gid;
        }

        static auto getSGID()
        {
            return sgid;
        }

        template<typename T>
        static auto& getTL(T&& table)
        {
            if (sgid + 1 > table.size())
                table.resize(sgid + 1);
            return table[sgid];
        }

        static void pushWait(const Task& task);
        static void pushDone(const Task& task);
        bool pushPending(const Task& task);

        void broadcast(const State& _, const std::memory_order& sync = std::memory_order_seq_cst);

    protected:
        void barrier(const State& post);

        void steal();

        void run();
    };
}
