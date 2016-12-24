#pragma once

#include <vector>
#include <atomic>
#include <thread>

#include "TLQ.hpp"

namespace tai
{
    class Worker;

    class Controller
    {
    public:
        std::vector<Worker> workers;
        const size_t concurrency;
        std::atomic<size_t> ready;

        explicit Controller(const size_t& concurrency = std::thread::hardware_concurrency());
    };
}
