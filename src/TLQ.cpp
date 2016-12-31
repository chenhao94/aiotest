#include <atomic>

#include "Log.hpp"
#include "TLQ.hpp"
#include "Controller.hpp"
#include "Worker.hpp"

namespace tai
{
    size_t TLQ::busy()
    {
        return Controller::ctrl->todo.load(std::memory_order_relaxed);
    }

    size_t TLQ::popTodo()
    {
        const auto ret = Controller::ctrl->todo.load(std::memory_order_relaxed);
        Controller::ctrl->todo.store(0, std::memory_order_relaxed);
        return ret;
    }

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
        Controller::ctrl->todo.fetch_add(ready.size(), std::memory_order_relaxed);
        setup(ready);
        if (current->size())
            Log::log("[", Worker::worker->id, "] ",
                    current->size(),
                    " task(s) have been added to queue <",
                    current == &wait,
                    current == &ready,
                    current == &done,
                    ">");
    }

    void TLQ::clearCurrent(const char* reason)
    {
        if (current)
        {
            if (reason && current->size())
                Log::log("[", Worker::worker->id, "] ",
                    current->size(),
                    " task(s) have been cleared from queue <",
                    current == &wait,
                    current == &ready,
                    current == &done,
                    "> (Reason: ", reason, ")");
            current->clear();
            if (!(rand() & 255))
                current->shrink_to_fit();
        }
    }
}
