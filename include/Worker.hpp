#pragma once

#include <memory>
#include <atomic>
#include <thread>
#include <array>
#include <functional>

#if defined(__unix__) || defined(__MACH__)
#include <unistd.h>
#endif

#ifdef _POSIX_VERSION
#include <errno.h>
#endif

#ifdef _POSIX_THREADS
#include <pthread.h>
#endif

#include <boost/lockfree/queue.hpp>

#include "Decl.hpp"
#include "Log.hpp"
#include "BTreeNodeBase.hpp"
#include "TLQ.hpp"
#include "State.hpp"
#include "Controller.hpp"

namespace tai
{
    class Worker
    {
        using State = WorkerState;
    protected:
        // Controller of this worker.
        Controller& ctrl;

        // Global ID pool.
        static std::atomic<size_t> poolSize;
        static boost::lockfree::queue<size_t> pool;

        // Global ID.
        size_t gid;
        static thread_local size_t sgid;

        // Cleanup mode.
        bool cleanup = false;

        // Thread-local queue.
        TLQ queue;

        // Worker thread handle.
        std::unique_ptr<std::thread> handle = nullptr;

        // Neighbor list used for tree-style communication.
        std::array<size_t, 2> neighbor;

    public:
        using Task = std::function<void()>;

        static thread_local Worker* worker;

        // Local ID in the controller.
        const size_t id;

        std::atomic<State> state = { Pending };
        std::atomic<bool> reject = { false };

        // Construct as the id-th worker of the controller.
        Worker(Controller& ctrl, size_t id);

        TAI_INLINE
        Worker(Worker&& _) noexcept :
            ctrl(_.ctrl),
            gid(_.gid),
            cleanup(_.cleanup),
            handle(std::move(_.handle)),
            neighbor(std::move(_.neighbor)),
            id(_.id),
            state(_.state.load(std::memory_order_relaxed)),
            reject(_.reject.load(std::memory_order_relaxed))
        {
        }

        TAI_INLINE
        ~Worker()
        {
            if (handle)
                handle->join();
            pool.push(gid);
        }

        // Get global thread ID;
        TAI_INLINE
        auto getGID() const
        {
            return gid;
        }

        // Get global thread ID;
        // This can only be called directly from the worker thread.
        TAI_INLINE
        static auto getSGID()
        {
            return sgid;
        }

        // Get the thread-local object via global thread ID.
        template<typename T, typename... Init>
        TAI_INLINE
        static auto& getTL(std::vector<std::unique_ptr<T>>& table, std::atomic_flag& mtx, Init&&... init)
        {
            while (mtx.test_and_set(std::memory_order_acquire));
            Log::debug("Fetching TL item for thread with global ID ", sgid);
            if (sgid < table.size())
            {
                auto ptr = table[sgid].get();
                if (ptr)
                    mtx.clear(std::memory_order_relaxed);
                else
                {
                    table[sgid].reset(ptr = new T(std::forward<Init>(init)...));
                    mtx.clear(std::memory_order_release);
                }
                return *ptr;
            }

            auto ptr = new T(std::forward<Init>(init)...);
            table.resize(sgid);
            table.emplace_back(ptr);
            mtx.clear(std::memory_order_release);
            return *ptr;
        }

        TAI_INLINE
        void go()
        {
            using namespace std;

            handle.reset(new thread([this](){ run(); }));

            #ifdef _POSIX_THREADS
            #ifndef __APPLE__
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            if (ctrl.affinity)
                CPU_SET(getGID() % thread::hardware_concurrency(), &cpuset);
            else
                for (auto i = thread::hardware_concurrency(); i--; CPU_SET(i, &cpuset));

            if (auto err = pthread_setaffinity_np(handle->native_handle(), sizeof(cpu_set_t), &cpuset))
                Log::log("Warning: Failed to set thread affinity (Error: ", err, " ", strerror(err), ").");
            #endif
            #endif
        }

        // Push task into the queue specified.
        static void pushWait(Task task);
        static void pushDone(Task task);
        bool pushPending(Task task);

    protected:
        // Decide whether this worker should close.
        bool closing();

        // Set worker state to "Sync", wait for all workers to sync and advance to next state.
        // The next state is decided by evaluating f() at master worker after everyone reaches "Sync".
        State barrier(std::function<State()> f);

        // Work stealing.
        void steal();

        // Main loop.
        void run();
    };
}
