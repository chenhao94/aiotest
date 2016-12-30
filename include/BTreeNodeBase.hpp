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
        std::atomic<bool> failed = { false };

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
        virtual void merge(char* ptr) = 0;

        // Unlink the subtree.
        virtual void drop() = 0;

        // Read/write [begin, end - 1] to/from ptr[0..end - begin].
        virtual void read(size_t begin, size_t end, char* ptr, IOCtrl* io) = 0;
        virtual void write(size_t begin, size_t end, const char* ptr, IOCtrl* io) = 0;
        // Flush subtree cache.
        virtual void flush(IOCtrl* io) = 0;
        // Flush node cache.
        virtual void flush() = 0;
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

        BTreeNodeBase(BTreeConfig& conf, size_t offset, BTreeNodeBase* parent = nullptr) : conf(conf), offset(offset), parent(parent)
        {
        }

        virtual ~BTreeNodeBase()
        {
        }

    protected:
        // Shared configuration.
        BTreeConfig& conf;

        std::atomic<size_t> lck = { 0 };
        char* data = nullptr;
        size_t offset = 0;
        BTreeNodeBase* parent = nullptr;
        bool dirty = false;

        // Read/write file.
        bool fread(char* buf, size_t pos, size_t len);
        bool fwrite(const char* buf, size_t pos, size_t len);

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
        void unlock(size_t num);
    };
}
