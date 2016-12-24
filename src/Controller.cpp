#include "Controller.hpp"
#include "Worker.hpp"

namespace tai
{
    Controller::Controller(const size_t& concurrency) : concurrency(concurrency)
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
}
