#pragma once

#include <algorithm>
#include <atomic>
#include <mutex>
#include <functional>
#include <vector>
#include <queue>

namespace tai
{
    class TLQ
    {
        std::atomic_flag lckPending = ATOMIC_FLAG_INIT;
        std::deque<std::function<void()>> pending;

        std::unique_ptr<std::vector<std::function<void()>>> wait = nullptr;
        std::unique_ptr<std::vector<std::function<void()>>> ready = nullptr;
        std::atomic<ssize_t> remain = {-1};

    public:
        TLQ():
            wait(new std::vector<std::function<void()>>),
            ready(new std::vector<std::function<void()>>)
        {
        }

        void roll()
        {
            ready->clear();
            ready->shrink_to_fit();
            swap(wait, ready);
            while (lckPending.test_and_set(std::memory_order_acquire));
            if (!pending.empty())
            {
                auto&& task = pending.front();
                pending.pop_front();
                lckPending.clear(std::memory_order_release);
                ready->emplace_back(task);
            }
            else
                lckPending.clear(std::memory_order_release);
            remain = (ssize_t)ready->size() - 1;
        }

        auto operator ()()
        {
            auto i = remain.fetch_sub(1, std::memory_order_relaxed);
            if (i >= 0)
                (*ready)[i]();
            return i;
        }

        template<typename Fn>
        auto& operator ()(Fn&& task)
        {
            return wait->emplace_back(std::forward<Fn>(task));
        }

        template<typename Fn>
        void push(Fn&& task)
        {
            while (lckPending.test_and_set(std::memory_order_acquire));
            pending.emplace_back(task);
            lckPending.clear(std::memory_order_release);
        }
    };
}
