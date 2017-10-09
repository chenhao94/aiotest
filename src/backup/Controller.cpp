#include <thread>

#include "Controller.hpp"
#include "Worker.hpp"

namespace tai
{
    thread_local Controller* Controller::ctrl = nullptr;

    Controller::Controller(size_t lower, size_t upper, ssize_t con) : concurrency(con + (con > 0 ? 0 : std::thread::hardware_concurrency())), lower(lower), upper(upper), cache(131072)
    {
        Log::debug("Constructing controller...");
        workers.reserve(concurrency);
        for (size_t i = 0; i < concurrency; workers.emplace_back(*this, i++));
        for (auto& i : workers)
            i.go();
    }

    Controller::~Controller()
    {
        Log::debug("Destructing controller...");
        for (auto& i : workers)
            i.reject.store(true, std::memory_order_relaxed);
        Log::debug("    All workers now rejected new requests.");
        ready.store(false, std::memory_order_relaxed);
        wait(Quit, std::memory_order_relaxed);
        Log::debug("    Controller destructed.");
    }

    void Controller::wait(WorkerState _, std::memory_order sync)
    {
        for (auto& i : workers)
            while (i.state.load(sync) != _);
    }
}
