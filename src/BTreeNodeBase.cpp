#include <string>
#include <memory>
#include <fstream>
#include <functional>

#include <boost/lockfree/queue.hpp>

#include "BTreeNodeBase.hpp"
#include "TLQ.hpp"
#include "Controller.hpp"
#include "Worker.hpp"

namespace tai
{
    BTreeConfig::BTreeConfig(const std::string& path) : path(path), file(path)
    {
    }

    BTreeConfig::~BTreeConfig()
    {
        if (file)
            file.close();
    }

    BTreeConfig::operator bool()
    {
        return file.is_open();
    }

    void BTreeConfig::operator()(BTreeNodeBase* const node, const size_t& size)
    {
        if (node->data)
        {
            Controller::ctrl->used.fetch_sub(size, std::memory_order_relaxed);
            delete[] node->data;
            node->data = nullptr;
        }
        else
        {
            Controller::ctrl->used.fetch_add(size, std::memory_order_relaxed);
            Controller::ctrl->cache.push(node);
            node->data = new char[size];
        }
    }

    bool BTreeNodeBase::valid()
    {
        return parent;
    }

    BTreeNodeBase::BTreeNodeBase(const std::shared_ptr<BTreeConfig>& conf, const size_t& offset, BTreeNodeBase* const parent) : conf(conf), offset(offset), parent(parent)
    {
    }

    size_t BTreeNodeBase::lock()
    {
        return lck.load(std::memory_order_relaxed);
    }

    void BTreeNodeBase::lock(const size_t& num)
    {
        lck.fetch_add(num, std::memory_order_relaxed);
    }

    void BTreeNodeBase::unlock()
    {
        Worker::pushDone([this](){ for (auto i = parent; i && i->lck.fetch_sub(1, std::memory_order_relaxed) == 1; i = i->parent); });
    }
}
