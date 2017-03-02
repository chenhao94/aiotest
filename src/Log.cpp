#include <atomic>

#include "Log.hpp"

namespace tai::Log
{
    std::atomic<ssize_t> barrier_time;
    std::atomic<ssize_t> steal_time;
    std::atomic<ssize_t> run_time;

    void debug_counter_add(std::atomic<ssize_t>& counter, ssize_t num)
    {
#ifdef TAI_DEBUG
        counter.fetch_add(num, std::memory_order_relaxed);
#endif
    }
}
