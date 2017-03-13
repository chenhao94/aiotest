#include <thread>
#include <chrono>
#include <functional>
#include <algorithm>

#ifdef __unix__
#include <unistd.h>
#endif

#ifdef _POSIX_VERSION
#include <errno.h>
#endif

#ifdef _POSIX_THREADS
#include <pthread.h>
#endif

#include "Log.hpp"
#include "BTreeNodeBase.hpp"
#include "Worker.hpp"

namespace tai
{
    Worker::Worker(Controller& ctrl, size_t id) : ctrl(ctrl), id(id)
    {
        if (!pool.pop(gid))
            gid = poolSize.fetch_add(1, std::memory_order_relaxed);
        neighbor[0] = id * neighbor.size() + 1;
            for (size_t i = 1; i < neighbor.size(); ++i)
                neighbor[i] = neighbor[i - 1] + 1;
        for (auto& i : neighbor)
            if (i >= ctrl.concurrency)
                i = -1;
    }

    void Worker::go()
    {
        using namespace std;

        handle.reset(new thread([this](){ run(); }));

        #ifdef _POSIX_THREADS
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        if (ctrl.affinity)
            CPU_SET(getGID() % thread::hardware_concurrency(), &cpuset);
        else
            for (auto i = thread::hardware_concurrency(); i--; CPU_SET(i, &cpuset));

        if (auto err = pthread_setaffinity_np(handle->native_handle(), sizeof(cpu_set_t), &cpuset))
            Log::log("Warning: Failed to set thread affinity (Error: ", err, " ", strerror(err), ").");
        #endif
    }

    void Worker::pushWait(Task task)
    {
        worker->queue.pushWait(task);
    }

    void Worker::pushDone(Task task)
    {
        worker->queue.pushDone(task);
    }

    bool Worker::pushPending(Task task)
    {
        if (reject.load(std::memory_order_relaxed))
            return false;
        Log::debug("[", id, "] Push a pending task");
        queue.pushPending(task);
        return true;
    }

    bool Worker::closing()
    {
        return !ctrl.ready.load(std::memory_order_relaxed);
    }

    Worker::State Worker::barrier(std::function<State()> f)
    {
        using namespace std;
        using namespace this_thread;
        using namespace chrono;

        #ifdef TAI_DEBUG
        auto start = high_resolution_clock::now();
        #endif

        if (id)
        {
            auto& master = ctrl.workers[0].state;
            for (auto prev = state.load(memory_order_relaxed); ;)
            {
                state.store(Sync, memory_order_release);
                auto post = master.load(memory_order_acquire);
                for (auto i = Controller::spin; i-- && (post == prev || post == Sync); post = master.load(memory_order_acquire));
                for (; post == prev || post == Sync; post = master.load(memory_order_acquire))
                    yield();
                // TODO
                // state.store(post, memory_order_release);
                state.store(post);
                if (master.load(memory_order_acquire) == post)
                {
                    #ifdef TAI_DEBUG
                    Log::debug_counter_add(Log::barrier_time, duration_cast<nanoseconds>(high_resolution_clock::now() - start).count());
                    #endif
                    // Log::log("[", id, "]     ", to_string(prev), " -> ", to_string(post));
                    return post;
                }
            }
        }

        // TODO
        // state.store(Sync, memory_order_release);
        state.store(Sync);
        for (auto& i : ctrl.workers)
        {
            for (auto j = Controller::spin; j-- && i.state.load(memory_order_acquire) != Sync;);
            for (; i.state.load(memory_order_acquire) != Sync; yield());
        }
        const auto post = f();
        // Log::log("[", id, "] Publishing ", to_string(post));
        state.store(post, memory_order_release);

        #ifdef TAI_DEBUG
        Log::debug_counter_add(Log::barrier_time, duration_cast<nanoseconds>(high_resolution_clock::now() - start).count());
        #endif
        // Log::log("[", id, "]     ", to_string(post));

        return post;
    }

    void Worker::steal()
    {
        using namespace std::chrono;
        auto start = high_resolution_clock::now();
        for (; queue(); /*Log::log("[", id, "] DIY")*/);
        for (auto i = id; ++i != ctrl.workers.size();)
            for (; ctrl.workers[i].queue(); /*Log::log("[", id, "] Steal from ", i)*/);
        for (size_t i = 0; i != id; ++i)
            for (; ctrl.workers[i].queue(); /*Log::log("[", id, "] Steal from ", i)*/);
        Log::debug_counter_add(Log::steal_time, duration_cast<nanoseconds>(high_resolution_clock::now() - start).count());
    }

    void Worker::run()
    {
        using namespace std;
        using namespace chrono;

        auto start = high_resolution_clock::now();

        Controller::ctrl = &ctrl;
        worker = this;
        sgid = gid;
        ssize_t roundIdle = 0;
        function<State()> decision = [](){ return Running; };
        for (auto entry = Idle; entry != Quit;)
            switch (entry = barrier(decision))
            {
            case Running:
                Log::debug("[", id, "] Steal");
                roundIdle = 0;
                steal();
                if (!id)
                    decision = [](){
                        for (auto& i : Controller::ctrl->workers)
                            i.queue.clearCurrent();
                        return GC;
                    };
                break;
            case Unlocking:
                steal();
                if (!id)
                    decision = [this](){
                        for (auto& i : Controller::ctrl->workers)
                            i.queue.clearCurrent("Done");
                        if (closing())
                            return Cleanup;
                        for (auto& i : Controller::ctrl->workers)
                            i.queue.roll();
                        return queue.popTodo() ? Running : Idle;
                    };
                break;
            case Cleanup:
                cleanup = true;
            case Idle:
                ++roundIdle;
            case GC:
                {
                    const auto exceed = roundIdle - Controller::roundIdle;
                    if (!roundIdle || ctrl.lower >> max(exceed - 1, (ssize_t)0))
                    {
                        const auto lower = cleanup ? 0 : ctrl.lower >> max(exceed, (ssize_t)0);
                        while (ctrl.used.load(memory_order_relaxed) > lower && ctrl.cache.consume_one([](auto i){ i->gc(); }));
                        if (!id)
                            decision = [](){
                                for (auto& i : Controller::ctrl->workers)
                                    i.queue.setupDone();
                                return Unlocking;
                            };
                    }
                    else if (!id)
                        decision = [this](){
                            if (closing())
                                return Quit;
                            for (auto& i : Controller::ctrl->workers)
                                i.queue.roll();
                            return queue.popTodo() ? Running : Idle;
                        };
                }
                break;
            default:
                break;
            }

        Log::debug_counter_add(Log::run_time, duration_cast<nanoseconds>(high_resolution_clock::now() - start).count());
    }

    std::atomic<size_t> Worker::poolSize;
    boost::lockfree::queue<size_t> Worker::pool(std::thread::hardware_concurrency());

    thread_local size_t Worker::sgid = -1;
    thread_local Worker* Worker::worker = nullptr;
}
