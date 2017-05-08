#include <cstdlib>
#include <string>
#include <memory>
#include <vector>
#include <fstream>
#include <functional>
#include <new>
#include <atomic>

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
    std::vector<std::unique_ptr<BTreeConfig>> BTreeConfig::pool;
    std::atomic_flag BTreeConfig::lck = ATOMIC_FLAG_INIT;

    void BTreeConfig::operator()(BTreeNodeBase* node, size_t size)
    {
        using namespace std;

        Log::debug(node->data ? "Release [" : "Allocate [", node->effective, "/", size, "]");

        node->effective = 0;
        if (node->data)
        {
            Controller::ctrl->used.fetch_sub(size, std::memory_order_relaxed);
            #ifdef TAI_JEMALLOC
            free(node->data);
            #else
            delete[] node->data;
            #endif
            node->data = nullptr;
        }
        else
        {
            Controller::ctrl->used.fetch_add(size, std::memory_order_relaxed);
            Controller::ctrl->cache.push(node);
            node->data =
                #ifdef TAI_JEMALLOC
                (char*)aligned_alloc(4096, size);
                #else
                new
                    #ifndef __MACH__
                    (align_val_t(4096))
                    #endif
                    char[size];
                #endif
        }
    }

    void BTreeNodeBase::unlock()
    {
        if (parent)
            Worker::pushDone([this](){ for (auto i = parent; i && i->lck.fetch_sub(1, std::memory_order_relaxed) == 1 && i->parent != i; i = i->parent); });
    }
}
