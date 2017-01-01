#pragma once

#include <string>
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

        enum Method
        {
            Lock,
            Timing
        };

        // Lock for on-going tasks related to this I/O request.
        std::atomic<size_t> dep = { 0 };

        // I/O request state.
        std::atomic<State> state = { Running };

        const Method method = Timing;

        // Lock for certain times.
        size_t lock(size_t num = 1)
        {
            if (method != Lock)
                return 0;
            return dep.fetch_add(num, std::memory_order_relaxed) + num;
        }

        // Unlock for certain times.
        size_t unlock(size_t num = 1)
        {
            if (method != Lock)
                return 0;
            return dep.fetch_sub(num, std::memory_order_release) - num;
        }

        // Check if it is locked.
        size_t locked()
        {
            if (method != Lock)
                return 0;

            if (const auto ret = dep.load(std::memory_order_relaxed))
                return ret;
            return dep.load(std::memory_order_acquire);
        }

        // Fail this request.
        void fail()
        {
            failed.store(true, std::memory_order_relaxed);
        }

        explicit IOCtrl(Method method = Timing) : method(method)
        {
            if (method == Lock)
                lock();
        }

        // Update the state assuming the request has completed.
        State notify()
        {
            if (failed.load(std::memory_order_relaxed))
            {
                state.store(Failed, std::memory_order_relaxed);
                return Failed;
            }

            state.store(Done, std::memory_order_seq_cst);
            return Done;
        }

        // Check and update the request state.
        State operator ()()
        {
            auto snapshot = state.load(std::memory_order_relaxed);
            if (snapshot != Running || method != Lock || locked())
                return snapshot;

            return notify();
        }
    };

    const char* to_cstring(IOCtrl::State _);
    const char* to_cstring(IOCtrl::Method _);
    std::string to_string(IOCtrl::State _);
    std::string to_string(IOCtrl::Method _);
}
