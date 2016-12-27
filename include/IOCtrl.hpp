#pragma once

#include <atomic>
#include <memory>

namespace tai
{
    class IOCtrl
    {
    public:
        enum State
        {
            Running,
            Failed,
            Rejected,
            Done
        };

        std::atomic<size_t> dep = { 0 };
        std::atomic<State> state = { Running };
        std::atomic<bool> failed = { false };

        auto lock(const size_t& num = 1)
        {
            return dep.fetch_add(num, std::memory_order_relaxed) + num;
        }

        auto unlock(const size_t& num = 1)
        {
            return dep.fetch_sub(num, std::memory_order_release) - num;
        }

        size_t locked()
        {
            if (const auto ret = dep.load(std::memory_order_relaxed))
                return ret;
            return dep.load(std::memory_order_acquire);
        }

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
