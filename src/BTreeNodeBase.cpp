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

    BTreeNodeBase::BTreeNodeBase(const std::shared_ptr<BTreeConfig>& conf, const size_t& offset) : conf(conf), offset(offset)
    {
    }

    BTreeNodeBase::BTreeNodeBase(Self&& _) : dirty(_.dirty), data(_.data), conf(std::move(_.conf)), offset(_.offset), invalid(_.invalid), flushing(_.flushing)
    {
        _.data = nullptr;
    }
}
