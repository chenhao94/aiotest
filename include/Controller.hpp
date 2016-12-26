#pragma once

#include <vector>
#include <atomic>
#include <thread>
#include <functional>

#include <boost/lockfree/queue.hpp>

#include "BTreeNodeBase.hpp"
#include "TLQ.hpp"
#include "State.hpp"
#include "Worker.hpp"

namespace tai
{
    class Worker;

    class Controller
    {
    public:
        class SafeTask
        {
            std::shared_ptr<BTreeNodeBase> host = nullptr;
            std::function<void()> task;

            template<typename T, typename Fn>
            SafeTask(T&& host, Fn&& task) : host(std::forward<T>(host)), task(std::forward<Fn>(task))
            {
            }

            ~SafeTask()
            {
                if (host)
                {
                    task();
                    host = nullptr;
                }
            }
        };

        static thread_local Controller* ctrl;
        std::vector<Worker> workers;
        const size_t concurrency;
        std::atomic_flag ready = ATOMIC_FLAG_INIT;

        const size_t lower;
        const size_t upper;
        std::atomic<size_t> used = {0};
        boost::lockfree::queue<SafeTask*> dirty;

        explicit Controller(const size_t& lower, const size_t& upper, const size_t& concurrency = std::thread::hardware_concurrency());

        ~Controller();

    protected:
        void wait(const WorkerState& _, const std::memory_order& sync = std::memory_order_seq_cst);
    };
}
