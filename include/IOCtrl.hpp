#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <memory>
#include <chrono>

#include "Decl.hpp"
#include "Log.hpp"

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

        const bool partial = false;

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

        IOCtrl(Method method = Timing, bool partial = false) : method(method), partial(partial)
        {
            if (method == Lock)
                lock();
        }

        explicit IOCtrl(bool partial) : method(Timing), partial(partial)
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

        // Wait for this request to complete by checking with the given interval.
        template<typename Rep, typename Period>
        auto wait(std::chrono::duration<Rep, Period> dur)
        {
            State state;
            for (; (state = (*this)()) == IOCtrl::Running; std::this_thread::sleep_for(dur));
            return state;
        }

        auto wait()
        {
            return wait(std::chrono::milliseconds(100));
        }
    };

    using IOCtrlHandle = std::unique_ptr<IOCtrl>;

    const char* to_cstring(IOCtrl::State _);
    const char* to_cstring(IOCtrl::Method _);
    std::string to_string(IOCtrl::State _);
    std::string to_string(IOCtrl::Method _);
}
