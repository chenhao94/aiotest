#include <iostream>
#include <thread>
#include <functional>
#include <algorithm>

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
        std::cerr << "[" + std::to_string(id) + "] Push a pending task\n" << std::flush;
        queue.pushPending(task);
        return true;
    }

    void Worker::broadcast(State _, std::memory_order sync)
    {
        for (auto& i : neighbor)
            if (~i)
                ctrl.workers[i].state.store(_, sync);
    }

    bool Worker::closing()
    {
        return !ctrl.ready.load(std::memory_order_relaxed);
    }

    Worker::State Worker::barrier(std::function<State()> f)
    {
        auto post = Sync;
        state.store(Sync, std::memory_order_release);
        if (id)
        {
            for (auto i = Controller::spin; i-- && (post = state.load(std::memory_order_relaxed)) == Sync;);
            if (post == Sync)
                for (; (post = state.load(std::memory_order_relaxed)) == Sync; std::this_thread::yield());
        }
        else
        {
            for (size_t i = 1; i < ctrl.concurrency; ++i)
            {
                for (auto j = Controller::spin; j-- && (post = ctrl.workers[i].state.load(std::memory_order_relaxed)) != Sync;);
                if (post != Sync)
                    for (; ctrl.workers[i].state.load(std::memory_order_relaxed) != Sync; std::this_thread::yield());
            }
            state.store(post = f(), std::memory_order_acquire);
        }
        /*
        std::cerr << "[" + std::to_string(id) + "] "
            + "Switch state to <" + to_string(post)
            + ">\n"
            << std::flush;
        */
        broadcast(post, std::memory_order_acquire);
        return post;
    }

    void Worker::steal()
    {
        while (queue());
        for (auto i = id; ++i != ctrl.workers.size();)
            while(ctrl.workers[i].queue());
        for (size_t i = 0; i != id; ++i)
            while(ctrl.workers[i].queue());
    }

    void Worker::run()
    {
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
                std::cerr << "[" + std::to_string(id) +"] Steal\n" << std::flush;
                roundIdle = 0;
                steal();
                barrier(GC);
            }
            queue.clearCurrent();
            const auto exceed = roundIdle - Controller::roundIdle;
            if (!roundIdle || ctrl.lower >> std::max(exceed - 1, (ssize_t)0))
            {
                const auto lower = cleanup ? 0 : ctrl.lower >> std::max(exceed, (ssize_t)0);
                while (ctrl.used.load(std::memory_order_relaxed) > lower && ctrl.cache.consume_one([](auto i){ i->valid() ? i->evict() : i->suicide(); }));
                queue.setupDone();
                barrier(Unlocking);
                steal();
                cleanup = barrier([this](){ return closing() ? GC : Pulling; }) == GC;
                queue.clearCurrent("Done");
            }
            else
            {
                // std::this_thread::yield();
                if (barrier([this](){ return closing() ? GC : Pulling; }) == GC)
                    break;
            }
        }
    }

    std::atomic<size_t> Worker::poolSize;
    boost::lockfree::queue<size_t> Worker::pool(std::thread::hardware_concurrency());

    thread_local size_t Worker::sgid = -1;
    thread_local Worker* Worker::worker = nullptr;
}
