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

        std::vector<Task> wait;
        std::vector<Task> ready;
        std::vector<Task> done;
        std::atomic<size_t> remain = {0};

        std::vector<Task>* current = nullptr;

    public:
        void roll();
        void setup(std::vector<Task>& queue);
        void setupReady();
        void setupDone();

        size_t operator ()();
        void pushWait(const Task& task);
        void pushDone(const Task& task);
        void pushPending(const Task& task);
    };
}
