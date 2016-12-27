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

    bool BTreeNodeBase::fread(char* const& buf, const size_t& pos, const size_t&len)
    {
        auto& file = Worker::getTL(conf->files);
        if (!file.is_open())
            file.open(conf->path, std::ios_base::in | std::ios_base::out | std::ios_base::binary);
        file.seekg(pos);
        return !file.read(buf, len).fail();
    }

    bool BTreeNodeBase::fwrite(char* const& buf, const size_t& pos, const size_t&len)
    {
        auto& file = Worker::getTL(conf->files);
        if (!file.is_open())
            file.open(conf->path, std::ios_base::in | std::ios_base::out | std::ios_base::binary);
        file.seekp(pos);
        return !file.write(buf, len).flush().fail();
    }

    void BTreeNodeBase::unlock()
    {
        if (parent)
            Worker::pushDone([this](){ for (auto i = parent; i && i->lck.fetch_sub(1, std::memory_order_relaxed) == 1; i = i->parent); });
    }

    void BTreeNodeBase::unlock(const size_t& num)
    {
        if (parent)
            Worker::pushDone([this, num](){ for (auto i = parent; i && i->lck.fetch_sub(num, std::memory_order_relaxed) == num; i = i->parent); });
    }
}
