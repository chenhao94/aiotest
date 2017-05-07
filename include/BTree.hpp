#pragma once

#include <cstdlib>
#include <string>
#include <memory>
#include <fstream>
#include <algorithm>
#include <mutex>
#include <vector>
#include <unordered_set>
#include <functional>
#include <random>
#include <chrono>

#include <boost/lockfree/queue.hpp>

#ifdef TAI_JEMALLOC
#include <jemalloc/jemalloc.h>
#endif

#include "Decl.hpp"
#include "Log.hpp"
#include "IOEngine.hpp"
#include "IOCtrl.hpp"
#include "BTreeNodeBase.hpp"
#include "BTreeBase.hpp"
#include "TLQ.hpp"
#include "Controller.hpp"
#include "Worker.hpp"

namespace tai
{
    template<size_t first, size_t... rest>
    class BTreeNode : public BTreeNodeBase
    {
        static_assert(first <= sizeof(size_t) * 8, "B-tree node cannot be larger than the size of address space.");
        static_assert((first + ... + rest) <= sizeof(size_t) * 8, "B-tree node cannot be larger than the size of address space.");

    public:
        using Self = BTreeNode<first, rest...>;
        using Child = BTreeNode<rest...>;

        static constexpr auto n = first;
        static constexpr auto N = (size_t)1 << n;
        static constexpr auto m = (rest + ...);
        static constexpr auto M = (size_t)1 << m;
        static constexpr auto nm = n + m;
        static constexpr auto NM = N << m;

        std::vector<Child*, Alloc<Child*>> child;

    private:
        // Convert file address into child indices.
        TAI_INLINE
        auto getRange(size_t begin, size_t end) const
        {
            return std::pair<size_t, size_t>{begin >> m & N - 1, end - 1 >> m & N - 1};
        }

        // Touch a child node to make sure it exists.
        TAI_INLINE
        auto touch(size_t i)
        {
            if (child.size() <= i)
                Log::debug("Cannot touch node ", i,
                        " within ", child.size(),
                        " node(s). Offset = ", offset ^ i << m);
            if (!child[i])
                child[i] = new Child(conf, offset ^ i << m, this);
            return child[i];
        }

        // Touch a child node to make sure it exists.
        // Use offset hint to reduce redundant computation.
        TAI_INLINE
        auto touch(size_t i, size_t offset)
        {
            if (child.size() <= i)
                Log::debug("Cannot touch node ", i,
                        " within ", child.size(),
                        " node(s). Offset hint = ", offset);
            if (!child[i])
                child[i] = new Child(conf, offset, this);
            return child[i];
        }

    public:
        TAI_INLINE
        BTreeNode(BTreeConfig& conf, size_t offset, BTreeNodeBase* parent) : BTreeNodeBase(conf, offset, this, parent), child(1)
        {
            Log::debug("Construct B-tree node [", offset, ", ", offset + NM, "].");
        }

        TAI_INLINE
        ~BTreeNode() override
        {
            Log::debug("Destruct B-tree node [", offset, ", ", offset + NM, "].");
            evict(true);
        }

        TAI_INLINE
        static void* operator new(size_t size)
        {
            return malloc(sizeof(Self));
        }

        TAI_INLINE
        static void* operator new[](size_t size)
        {
            return malloc(size);
        }

        TAI_INLINE
        static void operator delete(void* ptr)
        {
            free(ptr);
        }

        TAI_INLINE
        static void operator delete[](void* ptr)
        {
            free(ptr);
        }

        TAI_INLINE
        void merge(BTreeNodeBase* node, IOCtrl* io = nullptr) override
        {
            if (data)
            {
                const auto shift = offset - node->offset;
                node->prefetch(shift, io);
                memcpy(node->data + shift, data, effective);
                node->effective = shift + effective;
                parent = nullptr;
            }
            else
            {
                for (auto& i : child)
                    if (i)
                        i->merge(node, io);
                suicide();
            }
        }

        TAI_INLINE
        void drop(bool force) override
        {
            if (data)
                parent = force ? nullptr : this;
            else
            {
                for (auto& i : child)
                    if (i)
                        i->drop(force);
                suicide();
            }
        }

        TAI_INLINE
        void dropChild(bool force) override
        {
            for (auto& i : child)
                if (i)
                    i->drop(force);
            child.clear();
        }

        TAI_INLINE
        void detach(bool force) override
        {
            if (data)
            {
                if (!force)
                    evict(false);
                parent = nullptr;
            }
            else
            {
                for (auto& i : child)
                    if (i)
                    {
                        i->parent = force ? nullptr : i;
                        Worker::pushWait([=](){ i->detach(force); });
                    }
                suicide();
            }
        }

    private:
        // Merge subtree cache into node cache.
        TAI_INLINE
        bool merge(IOCtrl* io = nullptr)
        {
            if (Controller::ctrl->usage(NM) == Controller::Full)
                return false;

            conf(this, NM);
            for (auto& i : child)
                if (i)
                    i->merge(this, io);
            child.clear();
            return true;
        }

    public:
        TAI_INLINE
        void read(size_t begin, size_t end, char* ptr, IOCtrl* io) override
        {
            Log::debug("read {", offset, ", ", offset + NM, " : ", begin, ", ", end, "}");
            Log::debug("conf.io->size: ", conf.io->size);
            if (!NM && end > conf.io->size)
            {
                if (!io->partial || begin >= conf.io->size)
                {
                    io->fail();
                    unlock(io);
                    return;
                }
                end = conf.io->size;
            }

            if (NM && data)
            {
                cachedRead<NM>(begin, end, ptr, io);
                io->unlock();
            }
            else if (!NM || locked() || end - begin < NM)
            {
                const auto range = getRange(begin, end);
                Log::debug("range: ", range.first, ", ", range.second);
                lock(range.second - range.first + 1);
                io->lock(range.second - range.first);
                if (child.size() <= range.second)
                {
                    Log::debug("resizing: ", range.second + 1);
                    child.resize(range.second + 1);
                }

                auto node = touch(range.first, begin & -M);
                if (range.first == range.second)
                {
                    Log::debug("Push read task for children [", range.first, ", ", range.second, "] with data at ", (size_t)ptr, " for (", begin, ", " , end, ")");
                    Worker::pushWait([=](){ node->read(begin, end, ptr, io); });
                }
                else
                {
                    Log::debug("Push read task for children [", range.first, ", ", range.second, "] with data at ", (size_t)ptr, " for (", begin, ", " , (begin & -M) + M, ")");
                    Worker::pushWait([=](){ node->read(begin, (begin & -M) + M, ptr, io); });
                    auto suffix = ptr + (begin & M - 1 ? -begin & M - 1 : M);
                    for (auto i = range.first; ++i != range.second; suffix += M)
                    {
                        node = touch(i);
                        Log::debug("Push read task for children [", range.first, ", ", range.second, "] with data at ", (size_t)suffix, " for (", offset ^ i << m, ", " , offset ^ i + 1 << m, ")");
                        Worker::pushWait([=](){ node->read(offset ^ i << m, offset ^ i + 1 << m, suffix, io); });
                    }
                    node = touch(range.second, end - 1 & -M);
                    Log::debug("Push read task for children [", range.first, ", ", range.second, "] with data at ", (size_t)suffix, " for (", end - 1 & -M, ", " , end, ")");
                    Worker::pushWait([=](){ node->read(end - 1 & -M, end, suffix, io); });
                }
            }
            else if (merge(io))
            {
                cachedRead<NM>(begin, end, ptr, io);
                io->unlock();
            }
            else
            {
                fread(ptr, begin, NM, io);
                unlock(io);
            }
        }

        TAI_INLINE
        void write(size_t begin, size_t end, const char* ptr, IOCtrl* io) override
        {
            Log::debug("write {", offset, ", ", offset + NM, " : ", begin, ", ", end, "}");

            Log::debug("conf.io->size: ", conf.io->size);
            if (!NM && end > conf.io->size)
            {
                if (!fwrite("\0", end - 1, 1, io))
                {
                    unlock(io);
                    return;
                }
                conf.io->size = end;
            }

            if (NM && data)
            {
                Log::debug("Write data at ", (size_t)ptr, " for (", begin, ", ", end, ") to cache.");
                cachedWrite<NM>(begin, end, ptr, io);
                io->unlock();
            }
            else if (!NM || locked() || end - begin < NM)
            {
                const auto range = getRange(begin, end);
                lock(range.second - range.first + 1);
                io->lock(range.second - range.first);
                if (child.size() <= range.second)
                    child.resize(range.second + 1);

                auto node = touch(range.first, begin & -M);
                if (range.first == range.second)
                {
                    Log::debug("Push write task for children [", range.first, ", ", range.second, "] with data at ", (size_t)ptr, " for (", begin, ", " , end, ")");
                    Worker::pushWait([=](){ node->write(begin, end, ptr, io); });
                }
                else
                {
                    Log::debug("Push write task for children [", range.first, ", ", range.second, "] with data at ", (size_t)ptr, " for (", begin, ", " , (begin & -M) + M, ")");
                    Worker::pushWait([=](){ node->write(begin, (begin & -M) + M, ptr, io); });
                    auto suffix = ptr + (begin & M - 1 ? -begin & M - 1 : M);
                    for (auto i = range.first; ++i != range.second; suffix += M)
                    {
                        node = touch(i);
                        Log::debug("Push write task for children [", range.first, ", ", range.second, "] with data at ", (size_t)suffix, " for (", offset ^ i << m, ", " , offset ^ i + 1 << m, ")");
                        Worker::pushWait([=](){ node->write(offset ^ i << m, offset ^ i + 1 << m, suffix, io); });
                    }
                    node = touch(range.second, end - 1 & -M);
                    Log::debug("Push write task for children [", range.first, ", ", range.second, "] with data at ", (size_t)suffix, " for (", end - 1 & -M, ", " , end, ")");
                    Worker::pushWait([=](){ node->write(end - 1 & -M, end, suffix, io); });
                }
            }
            else
            {
                dropChild(true);
                if (Controller::ctrl->usage(NM) == Controller::Full)
                {
                    Log::debug("Write data at ", (size_t)ptr, " for (", begin, ", ", end, ") out. (Reason: No enough space to create cache page)");
                    fwrite(ptr, begin, end - begin, io);
                    unlock(io);
                }
                else
                {
                    Log::debug("Write data at ", (size_t)ptr, " for (", begin, ", ", end, ") to new cache.");
                    conf(this, NM);
                    memcpy(data, ptr, effective = NM);
                    dirty = true;
                    io->unlock();
                }
            }
        }

        TAI_INLINE
        static void sync(IOCtrl* io)
        {
            Worker::pushWait([=](){ Child::sync(io); });
        }

        TAI_INLINE
        static void sync(Task task, IOCtrl* io = nullptr)
        {
            Worker::pushWait([=](){ Child::sync(task, io); });
        }

        TAI_INLINE
        void flush(IOCtrl* io) override
        {
            if (dirty)
            {
                if (fwrite(data, offset, effective, io))
                {
                    dirty = false;
                    unlock();
                }
            }
            else if (!data && locked())
                for (auto& i : child)
                    if (i)
                        Worker::pushWait([=](){ i->flush(io); });
        }

        TAI_INLINE
        void evict(bool release) override
        {
            Log::debug("Evicting {", offset, ", ", offset + N, "}");
            if (dirty)
            {
                fwrite(data, offset, effective);
                dirty = false;
                unlock();
            }
            if (release && data)
                conf(this, NM);
        }
    };

    template<size_t first>
    class BTreeNode<first> : public BTreeNodeBase
    {
        static_assert(first <= sizeof(size_t) * 8, "B-tree node cannot be larger than the size of address space.");

    public:
        using Self = BTreeNode<first>;

        static constexpr auto n = first;
        static constexpr auto N = n < sizeof(size_t) * 8 ? (size_t)1 << n : (size_t)0;
        static constexpr auto m = 0;
        static constexpr auto M = (size_t)1;
        static constexpr auto nm = n;
        static constexpr auto NM = N;

        TAI_INLINE
        BTreeNode(BTreeConfig& conf, size_t offset, BTreeNodeBase* parent) : BTreeNodeBase(conf, offset, this, parent)
        {
            Log::debug("Construct B-tree node [", offset, ", ", offset + N, "].");
        }

        TAI_INLINE
        ~BTreeNode() override
        {
            Log::debug("Destruct B-tree node [", offset, ", ", offset + N, "].");
            evict(true);
        }

        TAI_INLINE
        static void* operator new(size_t size)
        {
            return malloc(sizeof(Self));
        }

        TAI_INLINE
        static void* operator new[](size_t size)
        {
            return malloc(size);
        }

        TAI_INLINE
        static void operator delete(void* ptr)
        {
            free(ptr);
        }

        TAI_INLINE
        static void operator delete[](void* ptr)
        {
            free(ptr);
        }

        TAI_INLINE
        void merge(BTreeNodeBase* node, IOCtrl* io = nullptr) override
        {
            if (data)
            {
                const auto shift = offset - node->offset;
                node->prefetch(shift, io);
                memcpy(node->data + shift, data, effective);
                node->effective = shift + effective;
                parent = nullptr;
            }
        }

        TAI_INLINE
        void drop(bool force) override
        {
            if (data)
                parent = force ? nullptr : this;
            else
                suicide();
        }

        TAI_INLINE
        void dropChild(bool force) override
        {
        }

        TAI_INLINE
        void detach(bool force) override
        {
            if (data)
            {
                if (!force)
                    evict(false);
                parent = nullptr;
            }
            else
                suicide();
        }

        TAI_INLINE
        void read(size_t begin, size_t end, char* ptr, IOCtrl* io) override
        {
            Log::debug("read(leaf) {", offset, ", ", offset + N, " : ", begin, ", ", end, "}");
            if (!N && end > conf.io->size)
            {
                if (!io->partial || begin >= conf.io->size)
                {
                    io->fail();
                    unlock(io);
                    return;
                }
                end = conf.io->size;
            }

            if (!N || !data && Controller::ctrl->usage(N) == Controller::Full)
            {
                Log::debug("Read from file.");
                fread(ptr, begin, end - begin, io);
                unlock(io);
            }
            else
            {
                Log::debug("Read from BTree cache.");
                if (!data)
                    conf(this, N);
                cachedRead<N>(begin, end, ptr, io);
                io->unlock();
            }
        }

        TAI_INLINE
        void write(size_t begin, size_t end, const char* ptr, IOCtrl* io) override
        {
            Log::debug("write(leaf) {", offset, ", ", offset + N, " : ", begin, ", ", end, "}");
            if (!N && end > conf.io->size)
            {
                if (!fwrite("0", end - 1, 1, io))
                {
                    unlock(io);
                    return;
                }
                conf.io->size = end;
            }

            if (!N || !data && Controller::ctrl->usage(N) == Controller::Full)
            {
                Log::debug("Write data at ", (size_t)ptr, " for (", begin, ", ", end, ") out. (Reason: No enough space to create cache page)");
                fwrite(ptr, begin, end - begin, io);
                unlock(io);
            }
            else
            {
                Log::debug("Write data at ", (size_t)ptr, " for (", begin, ", ", end, ") to new cache.");
                if (!data)
                    conf(this, N);
                cachedWrite<N>(begin, end, ptr, io);
                io->unlock();
            }
        }

        TAI_INLINE
        static void sync(IOCtrl* io)
        {
            io->notify();
        }

        TAI_INLINE
        static void sync(Task task, IOCtrl* io = nullptr)
        {
            task();
            if (io)
                io->notify();
        }

        TAI_INLINE
        void flush(IOCtrl* io) override
        {
            if (dirty)
            {
                Log::debug("Flush data at ", (size_t)data, " with file offset ", offset, " (effective = ", effective, ").");
                if (fwrite(data, offset, effective, io))
                {
                    dirty = false;
                    unlock();
                }
            }
        }

        TAI_INLINE
        void evict(bool release) override
        {
            Log::debug("Evicting {", offset, ", ", offset + N, "}");
            if (dirty)
            {
                fwrite(data, offset, effective);
                dirty = false;
                unlock();
            }
            if (release && data)
                conf(this, N);
        }
    };

    template<size_t... n>
    class BTree
    {
        static_assert(sizeof...(n), "Illegal template argument list.");
    };

    template<size_t n, size_t... rest>
    class BTree<n, rest...> : public BTreeBase
    {
        static_assert((n + ... + rest) == sizeof(size_t) * 8, "B-tree must cover the entire address space.");
    
    public:
        using Root = BTreeNode<n, rest...>;
        Root* root;

        static constexpr auto level = sizeof...(rest) + 1;
        static constexpr std::array<size_t, level> bits = {n, rest...};

        // Bind to the file from given path.
        TAI_INLINE
        explicit BTree(IOEngine* io) : BTreeBase(io), root(new Root(conf, 0, nullptr))
        {
            Log::debug("Construct B-tree for ", conf.io->str(), ".");
            mtxUsedID.lock();
            id = IDCount++;
            for (; usedID.count(id); id = rand());
            usedID.insert(id);
            mtxUsedID.unlock();
        }

        TAI_INLINE
        ~BTree() override
        {
            Log::debug("Destruct B-tree for \"", conf.io->str(), "\".");
            if (root)
                Log::log("Memory leak for at ", (size_t)root, ". Please detach before destruction.");
            std::lock_guard<std::mutex> lck(mtxUsedID);
            usedID.erase(id);
        }

        // Issue a read request to this file to the given controller.
        // Do not allow partial read.
        TAI_INLINE
        IOCtrl* read(Controller& ctrl, size_t pos, size_t len, char* ptr) override
        {
            Log::debug("Issued read: ", pos, ", " , len);
            auto io = new IOCtrl;
            if (len < 1)
                io->state.store(IOCtrl::Done, std::memory_order_relaxed);
            else
            {
                auto capture = root;
                if (!ctrl.workers[id % ctrl.workers.size()].pushPending([=](){ capture->read(pos, pos + len, ptr, io); }))
                {
                    Log::log("Read is rejected by pending queue.");
                    io->state.store(IOCtrl::Rejected, std::memory_order_relaxed);
                }
                else if (io->method != IOCtrl::Timing)
                {
                    Log::log("Read is rejected due to unsupported I/O method.");
                    io->state.store(IOCtrl::Rejected, std::memory_order_relaxed);
                }
                else if (!ctrl.workers[id % ctrl.workers.size()].pushPending([=](){ Root::sync(io); }))
                {
                    Log::log("Read is rejected by pending queue while submitting the timing task.");
                    io->state.store(IOCtrl::Rejected, std::memory_order_relaxed);
                }
            }
            return io;
        }

        // Issue a read request to this file to the given controller.
        // Allow partial read.
        TAI_INLINE
        IOCtrl* readsome(Controller& ctrl, size_t pos, size_t len, char* ptr) override
        {
            Log::debug("Issued readsome: ", pos, ", " , len);
            auto io = new IOCtrl;
            if (len < 1)
                io->state.store(IOCtrl::Done, std::memory_order_relaxed);
            else
            {
                auto capture = root;
                if (!ctrl.workers[id % ctrl.workers.size()].pushPending([=](){ capture->read(pos, pos + len, ptr, io); }))
                {
                    Log::log("Readsome is rejected by pending queue.");
                    io->state.store(IOCtrl::Rejected, std::memory_order_relaxed);
                }
                else if (io->method != IOCtrl::Timing)
                {
                    Log::log("Readsome is rejected due to unsupported I/O method.");
                    io->state.store(IOCtrl::Rejected, std::memory_order_relaxed);
                }
                else if (!ctrl.workers[id % ctrl.workers.size()].pushPending([=](){ Root::sync(io); }))
                {
                    Log::log("Readsome is rejected by pending queue while submitting the timing task.");
                    io->state.store(IOCtrl::Rejected, std::memory_order_relaxed);
                }
            }
            return io;
        }

        // Issue a write request to this file to the given controller.
        TAI_INLINE
        IOCtrl* write(Controller& ctrl, size_t pos, size_t len, const char* ptr) override
        {
            Log::debug("Issued write: ", pos, ", " , len);
            auto io = new IOCtrl;
            if (len < 1)
                io->state.store(IOCtrl::Done, std::memory_order_relaxed);
            else
            {
                auto capture = root;
                if (!ctrl.workers[id % ctrl.workers.size()].pushPending([=](){ capture->write(pos, pos + len, ptr, io); }))
                {
                    Log::log("Write is rejected by pending queue.");
                    io->state.store(IOCtrl::Rejected, std::memory_order_relaxed);
                }
                else if (io->method != IOCtrl::Timing)
                {
                    Log::log("Write is rejected due to unsupported I/O method.");
                    io->state.store(IOCtrl::Rejected, std::memory_order_relaxed);
                }
                else if (!ctrl.workers[id % ctrl.workers.size()].pushPending([=](){ Root::sync(io); }))
                {
                    Log::log("Write is rejected by pending queue while submitting the timing task.");
                    io->state.store(IOCtrl::Rejected, std::memory_order_relaxed);
                }
            }
            return io;
        }

        // Inject a task to pending list.
        TAI_INLINE
        bool inject(Controller& ctrl, std::function<void()> task) override
        {
            Log::debug("Issued inject");
            return ctrl.workers[id % ctrl.workers.size()].pushPending([=](){ Root::sync(task); });
        }

        // Inject a task and track for completion.
        TAI_INLINE
        IOCtrl* hook(Controller& ctrl, std::function<void()> task) override
        {
            Log::debug("Issued hook");
            auto io = new IOCtrl;
            if (!inject(ctrl, task))
            {
                Log::log("Sync(tree) is rejected while injecting the task.");
                io->state.store(IOCtrl::Rejected, std::memory_order_relaxed);
            }
            else if (!ctrl.workers[id % ctrl.workers.size()].pushPending([=](){ Root::sync(io); }))
            {
                Log::log("Hook is rejected by pending queue while submitting the timing task.");
                io->state.store(IOCtrl::Rejected, std::memory_order_relaxed);
            }
            return io;
        }

        // Issue a sync request to this file to the given controller.
        // Sync by recursively scan the B-tree.
        TAI_INLINE
        IOCtrl* syncTree(Controller& ctrl) override
        {
            Log::debug("Issued recursive sync");
            auto io = new IOCtrl;
            auto capture = root;
            if (!ctrl.workers[id % ctrl.workers.size()].pushPending([=](){ capture->flush(io); }))
            {
                Log::log("Sync(tree) is rejected by pending queue.");
                io->state.store(IOCtrl::Rejected, std::memory_order_relaxed);
            }
            else if (!ctrl.workers[id % ctrl.workers.size()].pushPending([=](){ Root::sync(io); }))
            {
                Log::log("Sync(tree) is rejected by pending queue while submitting the timing task.");
                io->state.store(IOCtrl::Rejected, std::memory_order_relaxed);
            }
            return io;
        }

        // Issue a sync request to this file to the given controller.
        // Sync by scanning controller's cache list.
        TAI_INLINE
        IOCtrl* syncCache(Controller& ctrl) override
        {
            Log::debug("Issued linear sync");
            return hook(ctrl, [&](){ ctrl.flush(); });
        }

        TAI_INLINE
        IOCtrl* sync(Controller& ctrl) override
        {
            return syncCache(ctrl);
        }

        // Issue a sync request to this file to the given controller.
        TAI_INLINE
        IOCtrl* detach(Controller& ctrl) override
        {
            Log::debug("Issued detach");
            auto io = new IOCtrl;
            auto capture = root;
            if (!root)
            {
                Log::log("Detach is rejected due to double free.");
                io->state.store(IOCtrl::Rejected, std::memory_order_relaxed);
            }
            else if (!ctrl.workers[id % ctrl.workers.size()].pushPending([=](){ capture->detach(false); }))
            {
                Log::log("Detach is rejected by pending queue.");
                io->state.store(IOCtrl::Rejected, std::memory_order_relaxed);
            }
            else if (!ctrl.workers[id % ctrl.workers.size()].pushPending([=](){ Root::sync(io); }))
            {
                Log::log("Detach is rejected by pending queue while submitting the timing task.");
                io->state.store(IOCtrl::Rejected, std::memory_order_relaxed);
            }
            else
                root = nullptr;
            return io;
        }

        // Issed a POSIX fsync().
        // This function can only be used with POSIXEngine.
        TAI_INLINE
        IOCtrl* fsync(Controller& ctrl) override
        {
            Log::debug("Issued fsync");
            inject(ctrl, [&](){ ctrl.flush(); });
            return hook(ctrl, [io(conf.io.get())](){ io->fsync(); });
        }
    };

    // Default hierarchy.
    using BTreeDefault = BTree<40, 3, 2, 3, 4, 12>;
    using BTreeTrivial = BTree<64>;
    using BTreeBinary = BTree<1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1>;
}
