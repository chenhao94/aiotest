#pragma once

#include <atomic>
#include <thread>
#include <functional>
#include <array>

#include "TLQ.hpp"
#include "State.hpp"
#include "Controller.hpp"

namespace tai
{
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

        Worker(Worker&& _) : ctrl(_.ctrl), id(_.id), foreign(_.foreign), handle(std::move(_.handle)), neighbor(std::move(_.neighbor))
        {
        }

        Worker(Controller& ctrl, const size_t& id) : ctrl(ctrl), id(id)
        {
            neighbor[0] = id * neighbor.size();
            if (neighbor.size() > 1)
                for (size_t i = 1; i < neighbor.size(); ++i)
                    neighbor[i] = neighbor[i - 1] + 1;
            else
                ++neighbor[0];
            for (auto& i : neighbor)
                if (i >= ctrl.concurrency)
                    i = -1;
            handle.reset(new std::thread([this](){ run(); }));
        }

        ~Worker()
        {
            if (handle)
                handle->join();
        }

        template<typename Fn>
        static auto& push(Fn&& task)
        {
            return queue(task);
        }

        void broadcast(const State& _, const std::memory_order& sync = std::memory_order_seq_cst)
        {
            for (auto& i : neighbor)
                if (~i)
                    ctrl.workers[i].state.store(_, sync);
        }

    protected:
        void barrier(const State& prev, const State& post, const std::memory_order& sync = std::memory_order_seq_cst)
        {
            if (id)
            {
                state.store(prev, sync);
                while (state.load(sync) != post);
                broadcast(post, sync);
            }
            else
            {
                for (size_t i = 1; i < ctrl.concurrency; ++i)
                    while (ctrl.workers[i].state.load(sync) != prev);
                state.store(post);
                broadcast(post, sync);
            }
        }

        void run()
        {
            foreign = &queue;
            state.store(Created, std::memory_order_release);
            while (state.load(std::memory_order_acquire) != Pulling);
            while (ctrl.ready.test_and_set())
            {
                queue.roll();
                barrier(Ready, Running);
                while (queue());
                for (auto i = id + 1; i != ctrl.workers.size(); ++i)
                    while((*ctrl.workers[i].foreign)());
                for (size_t i = 0; i != id; ++i)
                    while((*ctrl.workers[i].foreign)());
                barrier(Done, Pulling);
            }
        }
    };
}
