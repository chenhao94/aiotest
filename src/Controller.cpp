#include "Controller.hpp"
#include "Worker.hpp"

namespace tai
{
    Controller::Controller(const size_t& concurrency) : concurrency(concurrency)
    {
        workers.reserve(concurrency);
        for (size_t i = 0; i < concurrency; workers.emplace_back(*this, i++));
        for (auto& i : workers)
            while (i.state.load(std::memory_order_consume) != Worker::Ready);
        for (auto& i : workers)
            i.state.store(Worker::AllGreen, std::memory_order_release);
    }
}
