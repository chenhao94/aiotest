#pragma once

#include <algorithm>
#include <vector>
#include <atomic>
#include <thread>
#include <functional>

#include <boost/lockfree/queue.hpp>

#include "Decl.hpp"
#include "BTreeNodeBase.hpp"
#include "TLQ.hpp"
#include "State.hpp"
#include "Worker.hpp"

namespace tai
{
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

        // Idle time before dropping cache.
        static constexpr ssize_t roundIdle = 10;

        // Used for static reference from worker thread.
        // Worker may have member for this functionality but that's not accessible from other classes.
        static thread_local Controller* ctrl;

        // Workers.
        std::vector<Worker> workers;
        const size_t concurrency;
        const bool affinity = false;

        // Main loop switch.
        std::atomic<bool> ready = { true };

        // Flush switch
        std::atomic_flag flushing = ATOMIC_FLAG_INIT;

        // Number of tasks to do in this round.
        size_t todo = 0;

        // GC until memory usage goes down to lower bound.
        const size_t lower;
        // Refuse (but not strictly) to allocate cache memory beyond upper bound.
        const size_t upper;
        // Memory usage.
        std::atomic<size_t> used = { 0 };

        // Cahce queue for GC.
        boost::lockfree::queue<BTreeNodeBase*> cache;

        explicit Controller(size_t lower, size_t upper, ssize_t con = 0);

        ~Controller();

        // Ask if it is OK to allocate more memory.
        TAI_INLINE
        Usage usage(size_t alloc = 0)
        {
            const auto space = used.load(std::memory_order_relaxed) + alloc;
            if (space >= upper)
                return Full;
            if (space > lower)
                return High;
            if (space > 0)
                return Low;
            return Empty;
        }

        TAI_INLINE
        bool flush()
        {
            return flushing.test_and_set(std::memory_order_relaxed);
        }

    protected:
        // Wait until all worker reaches the given state.
        void wait(WorkerState _, std::memory_order sync = std::memory_order_seq_cst);
    };
}
