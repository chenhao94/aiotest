#pragma once

#include <atomic>

namespace tai
{
    class IOCtrl
    {
    public:
        std::atomic<size_t> dep = {0};

        size_t lock(const size_t& num = 1);
        size_t unlock(const size_t& num = 1);

        size_t locked();
    };
}
