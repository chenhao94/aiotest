#pragma once

#include <string>
#include <fstream>
#include <vector>
#include <functional>

#include "IOCtrl.hpp"

namespace tai
{
    class BTreeNodeBase;

    class BTreeConfig
    {
    public:
        const std::string path = "";
        std::vector<std::fstream> files;

        BTreeConfig(const std::string& path) : path(path)
        {
        }

        void operator()(BTreeNodeBase* const node, const size_t& size);
    };

    class BTreeNodeBase
    {
    public:
        friend class BTreeConfig;

        using Self = BTreeNodeBase;

        virtual void read(const size_t& begin, const size_t& end, char* const& ptr, IOCtrl* const& io) = 0;
        virtual void write(const size_t& begin, const size_t& end, char* const& ptr, IOCtrl* const& io) = 0;
        virtual void flush(IOCtrl* const& io) = 0;
        virtual void flush() = 0;
        virtual void evict() = 0;

        bool valid()
        {
            return parent;
        }

        void fail()
        {
            failed.store(std::memory_order_relaxed);
        }

        BTreeNodeBase(const std::shared_ptr<BTreeConfig>& conf, const size_t& offset, BTreeNodeBase* const parent = nullptr) : conf(conf), offset(offset), parent(parent)
        {
        }

        virtual ~BTreeNodeBase() = 0;

    protected:
        std::shared_ptr<BTreeConfig> conf = nullptr;

        std::atomic<size_t> lck = { 0 };
        char* data = nullptr;
        size_t offset = 0;
        BTreeNodeBase* parent = nullptr;
        bool dirty = false;
        std::atomic<bool> failed = { false };

        bool fread(char* const& buf, const size_t& pos, const size_t&len);
        bool fwrite(char* const& buf, const size_t& pos, const size_t&len);

        auto locked()
        {
            return lck.load(std::memory_order_relaxed);
        }

        void lock(const size_t& num = 1)
        {
            lck.fetch_add(num, std::memory_order_relaxed);
        }

        void unlock();
        void unlock(const size_t& num);
    };
}
