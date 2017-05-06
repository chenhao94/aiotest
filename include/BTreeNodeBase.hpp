#pragma once

#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <memory>
#include <vector>
#include <atomic>
#include <functional>

#include "Decl.hpp"
#include "Log.hpp"
#include "IOEngine.hpp"
#include "IOCtrl.hpp"

namespace tai
{
    class BTreeConfig
    {
    public:
        std::unique_ptr<IOEngine> io;
        std::atomic<bool> failed = { false };

        static std::vector<std::unique_ptr<BTreeConfig>> pool;
        static std::atomic_flag lck;

        TAI_INLINE
        explicit BTreeConfig(IOEngine* io) : io(io)
        {
            using namespace std;

            while (lck.test_and_set(memory_order_acquire));
            pool.emplace_back(this);
            lck.clear(memory_order_release);
        }

        void operator()(BTreeNodeBase* node, size_t size);
    };

    class BTreeNodeBase
    {
    public:
        friend class BTreeConfig;

        using Self = BTreeNodeBase;
        using Task = std::function<void()>;

        // Merge cache to ptr.
        virtual void merge(BTreeNodeBase* ptr, IOCtrl* io = nullptr) = 0;

        // Unlink the subtree.
        virtual void drop(bool force) = 0;

        // Unlink all the child subtree.
        virtual void dropChild(bool force) = 0;

        // Detach all the child nodes and delete this node.
        virtual void detach(bool force) = 0;

        // Read/write [begin, end - 1] to/from ptr[0..end - begin].
        virtual void read(size_t begin, size_t end, char* ptr, IOCtrl* io) = 0;
        virtual void write(size_t begin, size_t end, const char* ptr, IOCtrl* io) = 0;
        // Flush subtree cache.
        virtual void flush(IOCtrl* io) = 0;
        // Evict (flush+drop) node cache.
        virtual void evict(bool release) = 0;

        // Check if this subtree is still connected to the root.
        TAI_INLINE
        bool valid()
        {
            return parent;
        }

        // Recycle the cache.
        TAI_INLINE
        void gc()
        {
            if (valid())
            {
                evict(true);
                if (parent == this)
                    suicide();
            }
            else
                suicide();
        }

        // Set failed flag for this btree.
        // Used when no IOCtrl is responsible for the failure.
        TAI_INLINE
        void fail()
        {
            conf.failed.store(true, std::memory_order_relaxed);
        }

        // Delete this node.
        TAI_INLINE
        void suicide()
        {
            delete this;
        }

        // Prefetch no less than the given length into cache.
        TAI_INLINE
        bool prefetch(size_t len, IOCtrl* io = nullptr)
        {
            if (len > effective && data)
            {
                Log::debug("prefetch from file: ", offset + effective, ", ", len - effective, ", len: ", len);
                if (fread(data + effective, offset + effective, len - effective, io))
                    effective = len;
                else
                    fail();
            }
            else
                Log::debug("prefetch: len <= effective");
            return effective >= len;
        }

        TAI_INLINE
        BTreeNodeBase(BTreeConfig& conf, size_t offset, BTreeNodeBase* parent = nullptr) : conf(conf), offset(offset), parent(parent)
        {
        }

        BTreeNodeBase(const BTreeNodeBase &) = delete;

        TAI_INLINE
        virtual ~BTreeNodeBase()
        {
        }

    protected:
        // Shared configuration.
        BTreeConfig& conf;

        // Counter for the number of locked child.
        // A node is locked if it has any locked child, any dirty cache, or any task running on it.
        std::atomic<size_t> lck = { 0 };

    public:
        // Cache.
        char* data = nullptr;

        // Absolute offset for this subtree.
        const size_t offset = 0;

        // Length of valid cache prefix.
        // Set to 0 when there's no cahce.
        size_t effective = 0;

        // Parent pointer.
        // Set to nullptr while unlinking to ensure that different connected regions cannot access each other.
        BTreeNodeBase* parent = nullptr;

    protected:
        // Dirty flag for cache.
        // Set to false when there's no cache.
        bool dirty = false;

        // Read file.
        TAI_INLINE
        bool fread(char* buf, size_t pos, size_t len, IOCtrl* io = nullptr)
        {
            if (conf.io->read(buf, pos, len))
                return true;
            io ? io->fail() : fail();
            return false;
        }

        // Write file
        TAI_INLINE
        bool fwrite(const char* buf, size_t pos, size_t len, IOCtrl* io = nullptr)
        {
            Log::debug("Write ", len, " byte(s) of data at ", (size_t)buf, " to position ", pos, ".");
            if (conf.io->write(buf, pos, len))
                return true;
            Log::debug("Fwrite failed!");
            io ? io->fail() : fail();
            return false;
        }

        // Read via cache.
        // Bypass the cache if caching is too expensive.
        template<size_t N>
        TAI_INLINE
        bool cachedRead(size_t begin, size_t end, char* ptr, IOCtrl* io = nullptr)
        {
            const auto base = begin & N - 1;
            const auto len = end - begin;
            bool ret = true;

            if (base > effective && len < base - effective)
                ret = fread(ptr, begin, len, io);
            else if (ret = prefetch(end - offset))
                memcpy(ptr, data + base, len);
            unlock();

            return ret;
        }

        // Write via cache.
        // Bypass the cache if caching is too expensive.
        template<size_t N>
        TAI_INLINE
        bool cachedWrite(size_t begin, size_t end, const char* ptr, IOCtrl* io = nullptr)
        {
            const auto base = begin & N - 1;
            const auto len = end - begin;
            bool ret = true;

            std::ostringstream hex;
            hex << std::hex;
            for (size_t i = 0; i < 8 && begin + i < end; hex << std::setw(2) << std::setfill('0') << (int)ptr[i++]);

            if (base > effective && len < base - effective)
            {
                Log::debug("Redirect cached write [", begin, ",", end, "] begin with \"", hex.str(), "\" to disk due to large gap.");
                ret = fwrite(ptr, begin, len, io);
                unlock();
            }
            else
            {
                ret = prefetch(base);
                memcpy(data + base, ptr, len);
                if ((end - 1 & N - 1) >= effective)
                    effective = (end - 1 & N - 1) + 1;

                if (dirty || len < effective >> 1 && fwrite(ptr, begin, len, io))
                {
                    Log::debug("Flush small cached write [", begin, ",", end, "] begin with \"", hex.str(), "\" immediately to keep cache clean.");
                    unlock();
                }
                else
                {
                    Log::debug("Cached write [", begin, ",", end, "] begin with \"", hex.str(), "\" contaminates cache with effective = ", effective, ".");
                    dirty = true;
                }
            }

            return ret;
        }

        // Check if the subtree is locked.
        // A subtree is locked if any read/write is in progress.
        // In this case new read/write should respect them instead of simply drop their cache.
        // Dirty cache will also lock the the subtree until it is flushed.
        TAI_INLINE
        auto locked()
        {
            return lck.load(std::memory_order_relaxed);
        }

        // Lock this subtree.
        TAI_INLINE
        void lock(size_t num = 1)
        {
            lck.fetch_add(num, std::memory_order_relaxed);
        }

        // Unlock the subtree, including its parents if necessary.
        void unlock();

        // Unlock the subtree, including its parents if necessary.
        // Also unlock the given IOCtrl.
        TAI_INLINE
        void unlock(IOCtrl* const io)
        {
            unlock();
            io->unlock();
        }
    };
}
