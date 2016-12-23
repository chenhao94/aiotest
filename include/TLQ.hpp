#pragma once

#include <atomic>
#include <functional>
#include <vector>

namespace tai
{
    class TLQ
    {
        std::unique_ptr<std::vector<std::function<void()>>> wait = nullptr;
        std::unique_ptr<std::vector<std::function<void()>>> ready = nullptr;
        std::atomic<ssize_t> remain;

    public:
        TLQ(): wait(new std::vector<std::function<void()>>), ready(new std::vector<std::function<void()>>), remain(0)
        {
        }

        void roll()
        {
            ready->clear();
            ready->shrink_to_fit();
            swap(wait, ready);
            remain = (ssize_t)ready->size() - 1;
        }

        auto operator()()
        {
            auto i = remain.fetch_sub(1, std::memory_order_relaxed);
            if (i >= 0)
                (*ready)[i]();
            return i;
        }

        template<typename Fn>
        auto&& operator()(Fn&& task)
        {
            return wait->emplace_back(std::forward<Fn>(task));
        }
    };
}
