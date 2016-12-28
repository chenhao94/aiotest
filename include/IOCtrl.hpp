#pragma once

#include <atomic>
#include <memory>

namespace tai
{
    class IOCtrl
    {
    protected:
        std::atomic<bool> failed = { false };

    public:
        enum State
        {
            Running,
            Failed,
            Rejected,
            Done
        };

        // Lock for on-going tasks related to this I/O request.
        std::atomic<size_t> dep = { 0 };

        // I/O request state.
        std::atomic<State> state = { Running };

        // Lock for certain times.
        auto lock(const size_t& num = 1)
        {
            return dep.fetch_add(num, std::memory_order_relaxed) + num;
        }

        // Unlock for certain times.
        auto unlock(const size_t& num = 1)
        {
            return dep.fetch_sub(num, std::memory_order_release) - num;
        }

        // Check if it is locked.
        size_t locked()
        {
            if (const auto ret = dep.load(std::memory_order_relaxed))
                return ret;
            return dep.load(std::memory_order_acquire);
        }

        // Fail this request.
        void fail()
        {
            failed.store(true, std::memory_order_relaxed);
        }

        // Check and update the request state.
        State operator ()()
        {
            auto snapshot = state.load(std::memory_order_relaxed);
            if (snapshot != Running || locked())
                return snapshot;

            if (failed.load(std::memory_order_relaxed))
            {
                state.store(Failed, std::memory_order_relaxed);
                return Failed;
            }

            state.store(Done, std::memory_order_seq_cst);
            return Done;
        }
    };
}
