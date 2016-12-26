#pragma once

#include <atomic>
#include <thread>
#include <array>
#include <functional>

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
        using Task = std::function<void()>;

        std::atomic<State> state = {Pending};

        const size_t spin = 256;

        Worker(Worker&& _);

        Worker(Controller& ctrl, const size_t& id);

        ~Worker();

        static void pushWait(const Task& task);
        static void pushDone(const Task& task);
        static void pushPending(const Task& task);

        void broadcast(const State& _, const std::memory_order& sync = std::memory_order_seq_cst);

    protected:
        void barrier(const State& post, const std::memory_order& sync = std::memory_order_seq_cst);

        void steal();

        void run();
    };
}
