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
        std::vector<Task> done;
        std::atomic<size_t> remain = {0};

        std::vector<Task>* current = nullptr;

    public:

        TLQ();
        ~TLQ();

        void roll();
        void setup(std::vector<Task>* const& queue);
        void setupReady();
        void setupDone();

        size_t operator ()();
        void pushWait(const Task& task);
        void pushDone(const Task& task);
        void pushPending(const Task& task);
    };
}
