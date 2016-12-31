#include <string>
#include <memory>
#include <fstream>
#include <functional>

#include <boost/lockfree/queue.hpp>

#include "BTreeNodeBase.hpp"
#include "TLQ.hpp"
#include "Controller.hpp"
#include "Worker.hpp"
#include "IOCtrl.hpp"

namespace tai
{
    void BTreeConfig::operator()(BTreeNodeBase* node, size_t size)
    {
        node->effective = 0;
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

    bool BTreeNodeBase::prefetch(size_t len, IOCtrl* io)
    {
        if (len > effective && data)
            if (fread(data + effective, offset + effective, len - effective, io))
                effective += len;
            else
                fail();
        return effective >= len;
    }

    std::fstream& BTreeNodeBase::getFile()
    {
        auto& file = Worker::getTL(conf.files);
        if (file.fail())
            file.close();
        if (!file.is_open())
            file.open(conf.path, std::ios_base::in | std::ios_base::out | std::ios_base::binary);
        if (!file.is_open())
            file.open(conf.path, std::ios_base::in | std::ios_base::binary);
        if (!file.is_open())
            file.open(conf.path, std::ios_base::out | std::ios_base::binary);
        return file;
    }

    bool BTreeNodeBase::fread(char* buf, size_t pos, size_t len, IOCtrl* io)
    {
        if (getFile().seekg(pos).read(buf, len))
            return true;
        io ? io->fail() : fail();
        return false;
    }

    bool BTreeNodeBase::fwrite(const char* buf, size_t pos, size_t len, IOCtrl* io)
    {
        if (getFile().seekp(pos).write(buf, len).flush())
            return true;
        io ? io->fail() : fail();
        return false;
    }

    void BTreeNodeBase::unlock()
    {
        if (parent)
            Worker::pushDone([this](){ for (auto i = parent; i && i->lck.fetch_sub(1, std::memory_order_relaxed) == 1; i = i->parent); });
    }

    void BTreeNodeBase::unlock(size_t num)
    {
        if (parent)
            Worker::pushDone([this, num](){ for (auto i = parent; i && i->lck.fetch_sub(num, std::memory_order_relaxed) == num; i = i->parent); });
    }
}
