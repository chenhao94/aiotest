#pragma once

#include <atomic>
#include <memory>

namespace tai
{
    class IOCtrl
    {
    public:
        std::atomic<size_t> dep = {0};

        auto lock(const size_t& num = 1)
        {
            return dep.fetch_add(num, std::memory_order_relaxed) + num;
        }

        auto unlock(const size_t& num = 1)
        {
            return dep.fetch_sub(num, std::memory_order_release) - num;
        }

        size_t locked()
        {
            if (const auto ret = dep.load(std::memory_order_relaxed))
                return ret;
            return dep.load(std::memory_order_acquire);
        }
    };
}
