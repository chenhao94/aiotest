#pragma once

#include <atomic>
#include <thread>
#include <functional>

#include "TLQ.hpp"
#include "Controller.hpp"

namespace tai
{
    class Worker
    {
    protected:
        Controller& ctrl;
        static thread_local size_t id;
        static thread_local TLQ queue;
        TLQ* foreign;

        std::unique_ptr<std::thread> handle = nullptr;

    public:
        enum State
        {
            Pending,
            Ready,
            AllGreen,
            Pulling,
            Running,
            Syncing,
            Idle
        };

        std::atomic<State> state = {Pending};

        Worker(Worker&& _) : ctrl(_.ctrl), foreign(_.foreign), handle(std::move(_.handle))
        {
        }

        Worker(Controller& ctrl, const size_t& id) : ctrl(ctrl)
        {
            handle.reset(new std::thread([=](){ run(id); }));
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

    protected:
        void run(const size_t& id)
        {
            Worker::id = id;
            foreign = &queue;
            state.store(Ready, std::memory_order_release);
            while (state.load(std::memory_order_consume) != AllGreen);
            while (true)
            {
                queue.roll();
                state.store(Pulling, std::memory_order_release);
                queue.pull();
                state.store(Running, std::memory_order_release);
                while (queue());
                for (auto i = id + 1; i != ctrl.workers.size(); ++i)
                    while((*ctrl.workers[i].foreign)());
                for (size_t i = 0; i != id; ++i)
                    while((*ctrl.workers[i].foreign)());
                state.store(Syncing, std::memory_order_release);
                for (auto& i : ctrl.workers)
                    while (state.load(std::memory_order_consume) != Syncing);
            }
        }
    };
}
