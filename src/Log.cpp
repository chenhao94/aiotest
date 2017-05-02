#include <atomic>

#include "Log.hpp"

namespace tai::Log
{
    std::atomic<ssize_t> barrier_time;
    std::atomic<ssize_t> steal_time;
    std::atomic<ssize_t> run_time;
}
