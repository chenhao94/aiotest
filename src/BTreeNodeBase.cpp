#include <cstdlib>
#include <string>
#include <memory>
#include <fstream>
#include <functional>
#include <new>

#include <boost/lockfree/queue.hpp>

#ifdef TAI_JEMALLOC
#include <jemalloc/jemalloc.h>
#endif

#include "Log.hpp"
#include "BTreeNodeBase.hpp"
#include "TLQ.hpp"
#include "Controller.hpp"
#include "Worker.hpp"
#include "IOEngine.hpp"
#include "IOCtrl.hpp"

namespace tai
{
    void BTreeConfig::operator()(BTreeNodeBase* node, size_t size)
    {
        Log::debug(node->data ? "Release [" : "Allocate [", node->effective, "/", size, "]");

        node->effective = 0;
        if (node->data)
        {
            Controller::ctrl->used.fetch_sub(size, std::memory_order_relaxed);
            free(node->data);
            node->data = nullptr;
        }
        else
        {
            Controller::ctrl->used.fetch_add(size, std::memory_order_relaxed);
            Controller::ctrl->cache.push(node);
            #ifdef TAI_JEMALLOC
            node->data = (char*)aligned_alloc(4096, size);
            #else
            node->data = (char*)malloc(size);
            #endif
        }
    }

    void BTreeNodeBase::unlock()
    {
        if (parent)
            Worker::pushDone([this](){ for (auto i = parent; i && i->lck.fetch_sub(1, std::memory_order_relaxed) == 1 && i->parent != i; i = i->parent); });
    }
}
