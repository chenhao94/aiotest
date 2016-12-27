#include "IOCtrl.hpp"

#include <atomic>
#include <memory>

namespace tai
{
    size_t IOCtrl::lock(const size_t& num)
    {
        return dep.fetch_add(num, std::memory_order_relaxed) + num;
    }

    size_t IOCtrl::unlock(const size_t& num)
    {
        return dep.fetch_sub(num, std::memory_order_release) - num;
    }

    size_t IOCtrl::locked()
    {
        if (const auto ret = dep.load(std::memory_order_relaxed))
            return ret;
        return dep.load(std::memory_order_acquire);
    }
}
