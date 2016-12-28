#include <algorithm>
#include <atomic>
#include <thread>
#include <functional>
#include <vector>

#include <boost/lockfree/queue.hpp>

#include "TLQ.hpp"
#include "BTreeNodeBase.hpp"

namespace tai
{
    void TLQ::roll()
    {
        ready.swap(wait);
        Task* task = nullptr;
        if (pending.pop(task))
        {
            ready.emplace_back(*task);
            delete task;
            task = nullptr;
        }
    }

    void TLQ::setup(std::vector<Task>& queue)
    {
        if (current)
        {
            current->clear();
            if (!(rand() & 255))
                current->shrink_to_fit();
        }
        remain.store((current = &queue)->size(), std::memory_order_relaxed);
    }

    void TLQ::setupReady()
    {
        todo.fetch_add(ready.size(), std::memory_order_relaxed);
        setup(ready);
    }

    void TLQ::setupDone()
    {
        setup(done);
    }

    size_t TLQ::operator ()()
    {
        auto i = remain.fetch_sub(1, std::memory_order_relaxed);
        if (i)
            (*current)[i - 1]();
        return i;
    }

    void TLQ::pushWait(const Task& task)
    {
        wait.emplace_back(task);
    }

    void TLQ::pushDone(const Task& task)
    {
        done.emplace_back(task);
    }

    void TLQ::pushPending(const Task& task)
    {
        pending.push(new Task(task));
    }
}
