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

        // Spin time for hybrid mutex.
        static constexpr size_t spin = 256;

        // Used for static reference from worker thread.
        // Worker may have member for this functionality but that's not accessible from other classes.
        static thread_local Controller* ctrl;

        // Workers.
        std::vector<Worker> workers;
        const size_t concurrency;

        // Main loop switch.
        std::atomic_flag ready = ATOMIC_FLAG_INIT;

        // GC until memory usage goes down to lower bound.
        const size_t lower;
        // Refuse (but not strictly) to allocate cache memory beyond upper bound.
        const size_t upper;
        // Memory usage.
        std::atomic<size_t> used = {0};

        // Cahce queue for GC.
        boost::lockfree::queue<BTreeNodeBase*> cache;

        explicit Controller(const size_t& lower, const size_t& upper, const size_t& concurrency = std::thread::hardware_concurrency());

        ~Controller();

        // Ask if it is OK to allocate more memory.
        Usage usage(const size_t& alloc = 0);

    protected:
        // Wait until all worker reaches the given state.
        void wait(const WorkerState& _, const std::memory_order& sync = std::memory_order_seq_cst);
    };
}
