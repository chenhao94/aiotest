#include <thread>
#include <chrono>
#include <functional>
#include <algorithm>

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

    void Worker::broadcast(State _, std::memory_order sync)
    {
        for (auto& i : neighbor)
            if (~i)
            {
                auto& dst = ctrl.workers[i];
                if (dst.state.load(std::memory_order_relaxed) == Idle)
                    dst.broadcast(_, sync);
                else
                    dst.state.store(_, sync);
            }
    }

    bool Worker::closing()
    {
        return !ctrl.ready.load(std::memory_order_relaxed);
    }

    void Worker::idle()
    {
        auto prev = state.load(std::memory_order_relaxed);
        state.store(Idle, std::memory_order_release);
        std::this_thread::yield();
        state.store(prev, std::memory_order_acquire);
    }

    Worker::State Worker::barrier(std::function<State()> f)
    {
        using namespace std::chrono;
        auto start = high_resolution_clock::now();
        auto post = Sync;
        state.store(Sync, std::memory_order_release);
        if (id)
        {
            for (auto i = Controller::spin; i-- && (post = ctrl.state.load(std::memory_order_acquire)) == Sync;);
            if (post == Sync)
                for (; (post = ctrl.state.load(std::memory_order_acquire)) == Sync; std::this_thread::yield());
            state.store(post, std::memory_order_relaxed);
        }
        else
        {
            ctrl.state.store(Sync, std::memory_order_relaxed);
            for (size_t i = 1; i < ctrl.concurrency; ++i)
            {
                auto& dst = ctrl.workers[i].state;
                for (auto j = Controller::spin; j-- && (post = dst.load(std::memory_order_acquire)) != Sync;);
                if (post != Sync)
                    for (; dst.load(std::memory_order_acquire) != Sync; std::this_thread::yield());
            }
            state.store(post = f(), std::memory_order_relaxed);
            Log::debug("State changes to ", to_string(post));
            ctrl.state.store(post, std::memory_order_release);
        }
        Log::debug_counter_add(Log::barrier_time, duration_cast<nanoseconds>(high_resolution_clock::now() - start).count());
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
        using namespace std::chrono;
        auto start = high_resolution_clock::now();

        Controller::ctrl = &ctrl;
        worker = this;
        sgid = gid;
        state.store(Created, std::memory_order_release);
        while (state.load(std::memory_order_acquire) != Pulling);
        ssize_t roundIdle = 0;
        std::function<State()> decision = [](){ return Running; };
        for (auto entry = Pulling; entry != Quit;)
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
                --roundIdle;
            case Idle:
                ++roundIdle;
            case GC:
                {
                    const auto exceed = roundIdle - Controller::roundIdle;
                    if (!roundIdle || ctrl.lower >> std::max(exceed - 1, (ssize_t)0))
                    {
                        const auto lower = cleanup ? 0 : ctrl.lower >> std::max(exceed, (ssize_t)0);
                        while (ctrl.used.load(std::memory_order_relaxed) > lower && ctrl.cache.consume_one([](auto i){ i->gc(); }));
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

    /*
    void Worker::run()
    {
        using namespace std::chrono;
        auto start = high_resolution_clock::now();

        Controller::ctrl = &ctrl;
        worker = this;
        sgid = gid;
        state.store(Created, std::memory_order_release);
        while (state.load(std::memory_order_acquire) != Pulling);
        for (ssize_t roundIdle = 0; ;)
        {
            queue.roll();
            if (barrier([this](){ return queue.popTodo() ? Running : GC; }) == GC)
                ++roundIdle;
            else
            {
                Log::debug("[", id, "] Steal");
                roundIdle = 0;
                steal();
                barrier(GC);
            }
            queue.clearCurrent();
            const auto exceed = roundIdle - Controller::roundIdle;
            if (!roundIdle || ctrl.lower >> std::max(exceed - 1, (ssize_t)0))
            {
                const auto lower = cleanup ? 0 : ctrl.lower >> std::max(exceed, (ssize_t)0);
                while (ctrl.used.load(std::memory_order_relaxed) > lower && ctrl.cache.consume_one([](auto i){ i->gc(); }));
                queue.setupDone();
                barrier(Unlocking);
                steal();
                cleanup = barrier([this](){ return closing() ? GC : Pulling; }) == GC;
                queue.clearCurrent("Done");
            }
            else
            {
                // if (id)
                //     idle();
                // else
                //     std::this_thread::yield();
                if (barrier([this](){ return closing() ? Quit : Pulling; }) == Quit)
                    break;
            }
        }

        Log::debug_counter_add(Log::run_time, duration_cast<nanoseconds>(high_resolution_clock::now() - start).count());
    }
    */

    std::atomic<size_t> Worker::poolSize;
    boost::lockfree::queue<size_t> Worker::pool(std::thread::hardware_concurrency());

    thread_local size_t Worker::sgid = -1;
    thread_local Worker* Worker::worker = nullptr;
}
