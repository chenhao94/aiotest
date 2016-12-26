#pragma once

#include <atomic>
#include <thread>
#include <array>

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
        static thread_local TLQ queue;
        TLQ* foreign;

        std::unique_ptr<std::thread> handle = nullptr;

        std::array<size_t, 2> neighbor;

    public:
        std::atomic<State> state = {Pending};

        const size_t spin = 256;

        Worker(Worker&& _);

        Worker(Controller& ctrl, const size_t& id);

        ~Worker();

        template<typename Fn>
        static auto& push(Fn&& task)
        {
            return queue(task);
        }

        static void recycle(BTreeNodeBase* const node);
        static void recycle(std::vector<BTreeNodeBase*>& nodes);

        void broadcast(const State& _, const std::memory_order& sync = std::memory_order_seq_cst);

    protected:
        void barrier(const State& post, const std::memory_order& sync = std::memory_order_seq_cst);

        void run();
    };
}
