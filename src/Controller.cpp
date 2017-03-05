#include "Controller.hpp"
#include "Worker.hpp"

namespace tai
{
    thread_local Controller* Controller::ctrl = nullptr;

    Controller::Controller(size_t lower, size_t upper, size_t concurrency) : concurrency(concurrency), lower(lower), upper(upper), cache(131072)
    {
        workers.reserve(concurrency);
        for (size_t i = 0; i < concurrency; workers.emplace_back(*this, i++));
        for (auto& i : workers)
            i.go();
    }

    Controller::~Controller()
    {
        for (auto& i : workers)
            i.reject.store(true, std::memory_order_relaxed);
        ready.store(false, std::memory_order_relaxed);
        wait(Quit, std::memory_order_relaxed);
    }

    Controller::Usage Controller::usage(size_t alloc)
    {
        const auto space = used.load(std::memory_order_relaxed) + alloc;
        if (space >= upper)
            return Full;
        if (space > lower)
            return High;
        if (space > 0)
            return Low;
        return Empty;
    }

    void Controller::wait(WorkerState _, std::memory_order sync)
    {
        for (auto& i : workers)
            while (i.state.load(sync) != _);
    }
}
