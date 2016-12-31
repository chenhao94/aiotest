#pragma once

#include <string>
#include <fstream>
#include <algorithm>
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
        std::atomic<bool> failed = { false };
        size_t size = 0;

        BTreeConfig(const std::string& path) : path(path)
        {
        }

        void operator()(BTreeNodeBase* node, size_t size);
    };

    class BTreeNodeBase
    {
    public:
        friend class BTreeConfig;

        using Self = BTreeNodeBase;

        // Merge cache to ptr.
        virtual void merge(BTreeNodeBase* ptr, IOCtrl* io = nullptr) = 0;

        // Unlink the subtree.
        virtual void drop() = 0;

        // Read/write [begin, end - 1] to/from ptr[0..end - begin].
        virtual void read(size_t begin, size_t end, char* ptr, IOCtrl* io) = 0;
        virtual void write(size_t begin, size_t end, const char* ptr, IOCtrl* io) = 0;
        // Flush subtree cache.
        virtual void flush(IOCtrl* io) = 0;
        // Evict (flush+drop) node cache.
        virtual void evict() = 0;

        // Check if this subtree is still connected to the root.
        bool valid()
        {
            return parent;
        }

        // Set failed flag for this btree.
        // Used when no IOCtrl is responsible for the failure.
        void fail()
        {
            conf.failed.store(std::memory_order_relaxed);
        }

        // Delete this node.
        void suicide()
        {
            delete this;
        }

        // Prefetch no less than the given length into cache.
        bool prefetch(size_t len, IOCtrl* io = nullptr);

        BTreeNodeBase(BTreeConfig& conf, size_t offset, BTreeNodeBase* parent = nullptr) : conf(conf), offset(offset), parent(parent)
        {
        }

        BTreeNodeBase(const BTreeNodeBase &) = delete;

        virtual ~BTreeNodeBase()
        {
        }

    protected:
        // Shared configuration.
        BTreeConfig& conf;

        // Counter for the number of locked child.
        // A node is locked if it has any locked child, any dirty cache, or any task running on it.
        std::atomic<size_t> lck = { 0 };

        // Parent pointer.
        // Set to nullptr while unlinking to ensure that different connected regions cannot access each other.
        BTreeNodeBase* parent = nullptr;

    public:
        // Cache.
        char* data = nullptr;

        // Absolute offset for this subtree.
        const size_t offset = 0;

        // Length of valid cache prefix.
        // Set to 0 when there's no cahce.
        size_t effective = 0;

    protected:

        // Dirty flag for cache.
        // Set to false when there's no cache.
        bool dirty = false;

        // Get an opened file.
        std::fstream& getFile();

        // Read/write file.
        bool fread(char* buf, size_t pos, size_t len, IOCtrl* io = nullptr);
        bool fwrite(const char* buf, size_t pos, size_t len, IOCtrl* io = nullptr);

        // Read via cache.
        // Bypass the cache if caching is too expensive.
        template<size_t N>
        bool cachedRead(size_t begin, size_t end, char* ptr, IOCtrl* io = nullptr)
        {
            const auto base = begin & N - 1;
            const auto len = end - begin;
            bool ret = true;

            if (base > effective && len < base - effective)
                ret = fread(ptr, begin, len, io);
            else if (ret = prefetch(end & N - 1))
                memcpy(ptr, data + base, len);
            unlock();

            return ret;
        }

        // Write via cache.
        // Bypass the cache if caching is too expensive.
        template<size_t N>
        bool cachedWrite(size_t begin, size_t end, const char* ptr, IOCtrl* io = nullptr)
        {
            const auto base = begin & N - 1;
            const auto len = end - begin;
            bool ret = true;

            if (base > effective && len < base - effective)
            {
                ret = fwrite(ptr, begin, len, io);
                unlock();
            }
            else
            {
                ret = prefetch(base);
                memcpy(data + base, ptr, len);
                effective = std::max(effective, end & N - 1);

                if (dirty || len < effective >> 1 && fwrite(ptr, begin, len, io))
                    unlock();
                else
                    dirty = true;
            }

            return ret;
        }

        // Check if the subtree is locked.
        // A subtree is locked if any read/write is in progress.
        // In this case new read/write should respect them instead of simply drop their cache.
        // Dirty cache will also lock the the subtree until it is flushed.
        auto locked()
        {
            return lck.load(std::memory_order_relaxed);
        }

        // Lock this subtree.
        void lock(size_t num = 1)
        {
            lck.fetch_add(num, std::memory_order_relaxed);
        }

        // Unlock the subtree, including its parents if necessary.
        void unlock();
    };
}
