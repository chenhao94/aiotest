#include <thread>
#include <functional>

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
        State post;
        state.store(Sync, std::memory_order_release);
        if (id)
        {
            post = f();
            for (auto i = Controller::spin; i-- && state.load(std::memory_order_relaxed) == Sync;);
            for (; state.load(std::memory_order_relaxed) != Sync; std::this_thread::yield());
        }
        else
        {
            for (size_t i = 1; i < ctrl.concurrency; ++i)
            {
                for (auto j = Controller::spin; j-- && ctrl.workers[i].state.load(std::memory_order_relaxed) != Sync;);
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
        while (ctrl.ready.test_and_set())
        {
            queue.roll();
            queue.setupReady();
            barrier(Running);
            steal();
            barrier(GC);
            for (BTreeNodeBase* node; ctrl.used.load(std::memory_order_relaxed) > ctrl.lower && ctrl.cache.pop(node);)
                if (node->valid())
                    node->evict();
                else
                    delete node;
            queue.setupDone();
            barrier(Unlocking);
            steal();
            barrier(Pulling);
        }
    }
}
