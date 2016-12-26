#include "Controller.hpp"
#include "Worker.hpp"

namespace tai
{
    thread_local Controller* Controller::ctrl = nullptr;

    Controller::Controller(const size_t& lower, const size_t& upper, const size_t& concurrency) : concurrency(concurrency), lower(lower), upper(upper)
    {
        ready.test_and_set(std::memory_order_relaxed);
        workers.reserve(concurrency);
        for (size_t i = 0; i < concurrency; workers.emplace_back(*this, i++));
        wait(Created, std::memory_order_consume);
        for (auto& i : workers)
            i.state.store(Pulling, std::memory_order_acq_rel);
    }

    Controller::~Controller()
    {
        ready.clear(std::memory_order_relaxed);
    }

    void Controller::wait(const WorkerState& _, const std::memory_order& sync)
    {
        for (auto& i : workers)
            while (i.state.load(sync) != _);
    }

    Controller::Usage Controller::usage(const size_t& alloc)
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
}
