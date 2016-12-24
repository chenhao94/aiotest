#pragma once

#include <vector>
#include <atomic>
#include <thread>

#include "TLQ.hpp"
#include "State.hpp"

namespace tai
{
    class Worker;

    class Controller
    {
    public:
        std::vector<Worker> workers;
        const size_t concurrency;
        std::atomic_flag ready = ATOMIC_FLAG_INIT;

        explicit Controller(const size_t& concurrency = std::thread::hardware_concurrency());

        ~Controller();

    protected:
        void wait(const WorkerState& _, const std::memory_order& sync = std::memory_order_seq_cst);
    };
}
