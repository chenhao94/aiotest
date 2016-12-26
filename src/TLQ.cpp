#include <algorithm>
#include <atomic>
#include <functional>
#include <vector>

#include <boost/lockfree/queue.hpp>

#include "TLQ.hpp"
#include "BTreeNodeBase.hpp"

namespace tai
{
    TLQ::TLQ():
        wait(new std::vector<Task>),
        ready(new std::vector<Task>),
        garbage(new std::vector<std::shared_ptr<BTreeNodeBase>>),
        invalid(new std::vector<std::shared_ptr<BTreeNodeBase>>)
    {
    }

    TLQ::~TLQ()
    {
    }

    void TLQ::roll()
    {
        ready->clear();
        ready->shrink_to_fit();
        swap(wait, ready);
        Task* task = nullptr;
        if (pending.pop(task))
        {
            ready->emplace_back(*task);
            delete task;
            task = nullptr;
        }
        remain = (ssize_t)ready->size() - 1;
    }
}
