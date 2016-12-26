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

    auto BTreeConfig::operator()(const size_t& size)
    {
        Controller::ctrl->used.fetch_add(size, std::memory_order_relaxed);
        return new char[size];
    }

    auto BTreeConfig::operator()(char*& ptr, const size_t& size)
    {
        return Controller::ctrl->used.fetch_sub(size, std::memory_order_relaxed) - size;
        delete[] ptr;
        ptr = nullptr;
    }

    BTreeNodeBase::BTreeNodeBase(const std::shared_ptr<BTreeConfig>& conf, const size_t& offset) : conf(conf), offset(offset)
    {
    }

    BTreeNodeBase::BTreeNodeBase(Self&& _) : dirty(_.dirty), data(_.data), conf(std::move(_.conf)), offset(_.offset), invalid(_.invalid)
    {
        _.data = nullptr;
    }
}
