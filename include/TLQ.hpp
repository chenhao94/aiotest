#pragma once

#include <algorithm>
#include <atomic>
#include <functional>
#include <vector>

#include <boost/lockfree/queue.hpp>

#include "BTreeNodeBase.hpp"

namespace tai
{
    class TLQ
    {
    public:
        using Task = std::function<void()>;

    private:
        boost::lockfree::queue<Task*> pending;

        std::unique_ptr<std::vector<Task>> wait = nullptr;
        std::unique_ptr<std::vector<Task>> ready = nullptr;
        std::atomic<ssize_t> remain = {-1};

    public:
        std::unique_ptr<std::vector<std::shared_ptr<BTreeNodeBase>>> garbage;
        std::unique_ptr<std::vector<std::shared_ptr<BTreeNodeBase>>> invalid;

        TLQ();
        ~TLQ();

        void roll();

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
            pending.push(new Task(task));
        }
    };
}
