#include <iostream>
#include <atomic>

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
        if (ret)
            std::cerr << "Pop " + std::to_string(ret) + "\n" << std::flush;
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
            std::cerr << "[" + std::to_string(Worker::worker->id) + "] "
                + std::to_string(current->size())
                + " task(s) have been added to queue <"
                + std::to_string(current == &wait)
                + std::to_string(current == &ready)
                + std::to_string(current == &done)
                + ">\n"
                << std::flush;
    }

    void TLQ::clearCurrent(const char* reason)
    {
        if (current)
        {
            if (reason && current->size())
                std::cerr << "[" + std::to_string(Worker::worker->id) + "] "
                    + std::to_string(current->size())
                    + " task(s) have been cleared from queue <"
                    + std::to_string(current == &wait)
                    + std::to_string(current == &ready)
                    + std::to_string(current == &done)
                    + "> (Reason: " + reason + ")\n"
                    << std::flush;
            current->clear();
            if (!(rand() & 255))
                current->shrink_to_fit();
        }
    }
}
