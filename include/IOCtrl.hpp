#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <memory>
#include <chrono>

#ifdef TAI_JEMALLOC
#include <jemalloc/jemalloc.h>
#endif

#include "Decl.hpp"
#include "Log.hpp"
#include "Alloc.hpp"

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

        TAI_INLINE
        static void* operator new(size_t size)
        {
            return malloc(sizeof(IOCtrl));
        }

        TAI_INLINE
        static void* operator new[](size_t size)
        {
            return malloc(size);
        }

        TAI_INLINE
        static void operator delete(void* ptr)
        {
            free(ptr);
        }

        TAI_INLINE
        static void operator delete[](void* ptr)
        {
            free(ptr);
        }


        // Lock for certain times.
        TAI_INLINE
        size_t lock(size_t num = 1)
        {
            if (method != Lock)
                return 0;
            return dep.fetch_add(num, std::memory_order_relaxed) + num;
        }

        // Unlock for certain times.
        TAI_INLINE
        size_t unlock(size_t num = 1)
        {
            if (method != Lock)
                return 0;
            return dep.fetch_sub(num, std::memory_order_release) - num;
        }

        // Check if it is locked.
        TAI_INLINE
        size_t locked()
        {
            if (method != Lock)
                return 0;

            if (const auto ret = dep.load(std::memory_order_relaxed))
                return ret;
            return dep.load(std::memory_order_acquire);
        }

        // Fail this request.
        TAI_INLINE
        void fail()
        {
            failed.store(true, std::memory_order_relaxed);
        }

        TAI_INLINE
        IOCtrl(Method method = Timing, bool partial = false) : method(method), partial(partial)
        {
            if (method == Lock)
                lock();
        }

        TAI_INLINE
        explicit IOCtrl(bool partial) : method(Timing), partial(partial)
        {
            if (method == Lock)
                lock();
        }

        // Update the state assuming the request has completed.
        TAI_INLINE
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
        TAI_INLINE
        State operator ()()
        {
            auto snapshot = state.load(std::memory_order_relaxed);
            if (snapshot != Running || method != Lock || locked())
                return snapshot;

            return notify();
        }

        // Wait for this request to complete by checking with the given interval.
        template<typename Rep, typename Period>
        TAI_INLINE
        auto wait(std::chrono::duration<Rep, Period> dur)
        {
            State state;
            for (; (state = (*this)()) == IOCtrl::Running; std::this_thread::sleep_for(dur));
            return state;
        }

        TAI_INLINE
        auto wait()
        {
            return wait(std::chrono::milliseconds(100));
        }
    };

    using IOCtrlHandle = std::unique_ptr<IOCtrl>;
    using IOCtrlVec = std::vector<IOCtrlHandle, Alloc<IOCtrlHandle>>;

    TAI_INLINE
    static const char* to_cstring(IOCtrl::State _)
    {
        static const char* str[] = {
            "Running",
            "Failed",
            "Rejected",
            "Done"
        };

        return str[_];
    }

    TAI_INLINE
    static const char* to_cstring(IOCtrl::Method _)
    {
        static const char* str[] = {
            "Lock",
            "Timing"
        };

        return str[_];
    }

    TAI_INLINE
    static std::string to_string(IOCtrl::State _)
    {
        return to_cstring(_);
    }

    TAI_INLINE
    static std::string to_string(IOCtrl::Method _)
    {
        return to_cstring(_);
    }
}
