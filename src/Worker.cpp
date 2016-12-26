#include <thread>
#include <functional>

#include "Worker.hpp"

namespace tai
{
    Worker::Worker(Worker&& _) : ctrl(_.ctrl), id(_.id), foreign(_.foreign), handle(std::move(_.handle)), neighbor(std::move(_.neighbor))
    {
    }

    Worker::Worker(Controller& ctrl, const size_t& id) : ctrl(ctrl), id(id)
    {
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
    }

    void Worker::broadcast(const State& _, const std::memory_order& sync)
    {
        for (auto& i : neighbor)
            if (~i)
                ctrl.workers[i].state.store(_, sync);
    }

    void Worker::barrier(const State& post, const std::memory_order& sync)
    {
        if (id)
        {
            state.store(Sync, sync);
            for (auto i = spin; i-- && state.load(sync) != post;);
            for (; state.load(sync) != post; std::this_thread::yield());
            broadcast(post, sync);
        }
        else
        {
            for (size_t i = 1; i < ctrl.concurrency; ++i)
            {
                for (auto j = spin; j-- && ctrl.workers[i].state.load(sync) != Sync;);
                for (; ctrl.workers[i].state.load(sync) != Sync; std::this_thread::yield());
            }
            state.store(post);
            broadcast(post, sync);
        }
    }

    void Worker::run()
    {
        Controller::ctrl = &ctrl;
        foreign = &queue;
        state.store(Created, std::memory_order_release);
        while (state.load(std::memory_order_acquire) != Pulling);
        while (ctrl.ready.test_and_set())
        {
            queue.roll();
            barrier(Running);
            while (queue());
            for (auto i = id + 1; i != ctrl.workers.size(); ++i)
                while((*ctrl.workers[i].foreign)());
            for (size_t i = 0; i != id; ++i)
                while((*ctrl.workers[i].foreign)());
            barrier(GC);
            queue.invalid->clear();
            for (auto& i : *queue.garbage)
                i->invalidate();
            swap(queue.garbage, queue.invalid);
            barrier(Flushing);
            // for (BTreeNodeBase* node; ctrl.used.load(std::memory_order_relaxed) > ctrl.lower && ctrl.dirty.pop(node); node->flush());
            barrier(Pulling);
        }
    }
}
