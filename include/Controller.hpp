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
        enum Usage
        {
            Empty,
            Low,
            High,
            Full
        };

        class SafeNode
        {
        public:
            std::shared_ptr<BTreeNodeBase> node = nullptr;

            template<typename T>
            explicit SafeNode(T&& node) : node(std::forward<T>(node))
            {
            }

            ~SafeNode()
            {
            }
        };

        static thread_local Controller* ctrl;
        std::vector<Worker> workers;
        const size_t concurrency;
        std::atomic_flag ready = ATOMIC_FLAG_INIT;

        const size_t lower;
        const size_t upper;
        std::atomic<size_t> used = {0};
        boost::lockfree::queue<SafeNode*> dirty;

        explicit Controller(const size_t& lower, const size_t& upper, const size_t& concurrency = std::thread::hardware_concurrency());

        ~Controller();

        Usage usage(const size_t& alloc = 0);

    protected:
        void wait(const WorkerState& _, const std::memory_order& sync = std::memory_order_seq_cst);
    };
}
