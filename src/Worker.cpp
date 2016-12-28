#include <thread>
#include <functional>
#include <algorithm>

#include "BTreeNodeBase.hpp"
#include "Worker.hpp"

namespace tai
{
    Worker::Worker(Worker&& _) : ctrl(_.ctrl), id(_.id), foreign(_.foreign), handle(std::move(_.handle)), neighbor(std::move(_.neighbor))
    {
    }

    Worker::Worker(Controller& ctrl, const size_t& id) : ctrl(ctrl), id(id)
    {
        if (!pool.pop(gid))
            gid = poolSize.fetch_add(1, std::memory_order_relaxed);
        neighbor[0] = id * neighbor.size();
        if (neighbor.size() > 1)
            for (size_t i = 1; i < neighbor.size(); ++i)
                neighbor[i] = neighbor[i - 1] + 1;
        else
            ++neighbor[0];
        for (auto& i : neighbor)
            if (i >= ctrl.concurrency)
                i = -1;
        handle.reset(new std::thread([this](){ run(); }));
    }

    Worker::~Worker()
    {
        if (handle)
            handle->join();
        pool.push(gid);
    }

    void Worker::pushWait(const Task& task)
    {
        queue.pushWait(task);
    }

    void Worker::pushDone(const Task& task)
    {
        queue.pushDone(task);
    }

    bool Worker::pushPending(const Task& task)
    {
        if (block.test_and_set())
            return false;
        queue.pushPending(task);
        return true;
    }

    void Worker::broadcast(const State& _, const std::memory_order& sync)
    {
        for (auto& i : neighbor)
            if (~i)
                ctrl.workers[i].state.store(_, sync);
    }

    Worker::State Worker::barrier(const State& post)
    {
        barrier([post](){ return post; });
        return post;
    }

    Worker::State Worker::barrier(const std::function<State()>& f)
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
        broadcast(post, std::memory_order_acquire);
        return post;
    }

    void Worker::steal()
    {
        while (queue());
        for (auto i = id + 1; i != ctrl.workers.size(); ++i)
            while((*ctrl.workers[i].foreign)());
        for (size_t i = 0; i != id; ++i)
            while((*ctrl.workers[i].foreign)());
    }

    void Worker::run()
    {
        Controller::ctrl = &ctrl;
        sgid = gid;
        foreign = &queue;
        state.store(Created, std::memory_order_release);
        while (state.load(std::memory_order_acquire) != Pulling);
        for (ssize_t roundIdle = 0; ctrl.ready.test_and_set();)
        {
            queue.roll();
            queue.setupReady();
            if (barrier([](){ return queue.busy() ? Running : GC; }) == Running)
            {
                roundIdle = 0;
                steal();
                barrier(GC);
            }
            else
                ++roundIdle;
            const auto lower = ctrl.lower >> std::max(roundIdle - Controller::roundIdle, (ssize_t)0);
            for (BTreeNodeBase* node; ctrl.used.load(std::memory_order_relaxed) > lower && ctrl.cache.pop(node); node->valid() ? node->evict() : node->suicide());
            queue.setupDone();
            barrier(Unlocking);
            steal();
            barrier(Pulling);
        }
    }
}
