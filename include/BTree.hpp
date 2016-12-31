#pragma once

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

#include "IOCtrl.hpp"
#include "BTreeNodeBase.hpp"
#include "BTreeBase.hpp"
#include "TLQ.hpp"
#include "Controller.hpp"
#include "Worker.hpp"

namespace tai
{
    template<size_t n, size_t... rest>
    class BTreeNode : public BTreeNodeBase
    {
        static_assert(n <= sizeof(size_t) * 8, "B-tree node cannot be larger than the size of address space.");
        static_assert((n + ... + rest) <= sizeof(size_t) * 8, "B-tree node cannot be larger than the size of address space.");

    public:
        using Self = BTreeNode<n, rest...>;
        using Child = BTreeNode<rest...>;

        static constexpr auto N = (size_t)1 << n;
        static constexpr auto m = (rest + ...);
        static constexpr auto M = (size_t)1 << m;
        static constexpr auto nm = n + m;
        static constexpr auto NM = N << m;

        std::vector<Child*> child;

    private:
        // Convert file address into child indices.
        auto getRange(size_t begin, size_t end) const
        {
            return std::pair<size_t, size_t>{begin >> m & N - 1, end - 1 >> m & N - 1};
        }

        // Touch a child node to make sure it exists.
        auto touch(size_t i)
        {
            if (!child[i])
                child[i] = new Child(conf, offset ^ i << m, this);
            return child[i];
        }

        // Touch a child node to make sure it exists.
        // Use offset hint to reduce redundant computation.
        auto touch(size_t i, size_t offset)
        {
            if (!child[i])
                child[i] = new Child(conf, offset, this);
            return child[i];
        }

    public:
        BTreeNode(BTreeConfig& conf, size_t offset, BTreeNodeBase* parent = nullptr) : BTreeNodeBase(conf, offset, parent), child(1)
        {
        }

        ~BTreeNode() override
        {
            evict();
        }

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

        void drop() override
        {
            if (data)
                parent = nullptr;
            else
            {
                for (auto& i : child)
                    if (i)
                        i->drop();
                suicide();
            }
        }

    private:
        // Merge subtree cache into node cache.
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

        // Unlink all child subtrees.
        void dropChild()
        {
            for (auto& i : child)
                if (i)
                    i->drop();
            child.clear();
        }

    public:
        void read(size_t begin, size_t end, char* ptr, IOCtrl* io) override
        {
            if (!NM && end > conf.size)
            {
                io->fail();
                unlock();
                io->unlock();
                return;
            }

            if (NM && data)
            {
                cachedRead<NM>(begin, end, ptr, io);
                io->unlock();
            }
            else if (!NM || locked() || end - begin < NM)
            {
                const auto range = getRange(begin, end);
                lock(range.second - range.first + 1);
                io->lock(range.second - range.first);
                if (child.size() < range.second)
                    child.resize(range.second);

                auto node = touch(range.first, begin & -M);
                if (range.first == range.second)
                    Worker::pushWait([=](){ node->read(begin, end, ptr, io); });
                else
                {
                    Worker::pushWait([=](){ node->read(begin, (begin & -M) + M, ptr, io); });
                    auto suffix = ptr + (begin & M - 1 ? -begin & M - 1 : M);
                    for (auto i = range.first; ++i != range.second; suffix += M)
                    {
                        node = touch(i);
                        Worker::pushWait([=](){ node->read(offset ^ i << m, offset ^ i + 1 << m, suffix, io); });
                    }
                    node = touch(range.second, end - 1 & -M);
                    Worker::pushWait([=](){ node->read(end - 1 & -M, end, suffix, io); });
                }
            }
            else
            {
                if (merge(io))
                    cachedRead<NM>(begin, end, ptr, io);
                else
                {
                    fread(ptr, begin, NM, io);
                    unlock();
                }
                io->unlock();
            }
        }

        void write(size_t begin, size_t end, const char* ptr, IOCtrl* io) override
        {
            std::cerr << "{" + std::to_string(offset) + ", " + std::to_string(offset + NM) + "} [" + std::to_string(begin) + ", " + std::to_string(end) + "]\n" << std::flush;

            if (!NM && end > conf.size)
                if (fwrite("\0", end - 1, 1, io))
                    conf.size = end;
                else
                {
                    unlock();
                    io->unlock();
                    return;
                }

            if (NM && data)
            {
                cachedWrite<NM>(begin, end, ptr, io);
                io->unlock();
            }
            else if (!NM || locked() || end - begin < NM)
            {
                const auto range = getRange(begin, end);
                lock(range.second - range.first + 1);
                io->lock(range.second - range.first);
                if (child.size() < range.second)
                    child.resize(range.second);

                auto node = touch(range.first, begin & -M);
                if (range.first == range.second)
                    Worker::pushWait([=](){ node->write(begin, end, ptr, io); });
                else
                {
                    Worker::pushWait([=](){ node->write(begin, (begin & -M) + M, ptr, io); });
                    auto suffix = ptr + (-begin & M - 1);
                    for (auto i = range.first; ++i != range.second; suffix += M)
                    {
                        node = touch(i);
                        Worker::pushWait([=](){ node->write(begin & -NM ^ i << m, begin & -NM ^ i + 1 << m, suffix, io); });
                    }
                    node = touch(range.second, end - 1 & -M);
                    Worker::pushWait([=](){ node->write(end - 1 & -M, end, suffix, io); });
                }
            }
            else
            {
                dropChild();
                if (Controller::ctrl->usage(NM) == Controller::Full)
                {
                    fwrite(ptr, begin, end - begin, io);
                    unlock();
                }
                else
                {
                    conf(this, NM);
                    memcpy(data, ptr, effective = NM);
                    dirty = true;
                }
                io->unlock();
            }
        }

        static void sync(IOCtrl* io)
        {
            Worker::pushWait([=](){ Child::sync(io); });
        }

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
            else if (locked())
                for (auto& i : child)
                    Worker::pushWait([=](){ i->flush(io); });
        }

        void evict() override
        {
            if (dirty)
            {
                fwrite(data, offset, effective);
                dirty = false;
                unlock();
            }
            if (data)
                conf(this, NM);
        }
    };

    template<size_t n>
    class BTreeNode<n> : public BTreeNodeBase
    {
        static_assert(n <= sizeof(size_t) * 8, "B-tree node cannot be larger than the size of address space.");

    public:
        using Self = BTreeNode<n>;

        static constexpr auto N = (size_t)1 << n;

        BTreeNode(BTreeConfig& conf, size_t offset, BTreeNodeBase* parent = nullptr) : BTreeNodeBase(conf, offset, parent)
        {
        }

        ~BTreeNode() override
        {
            evict();
        }

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

        void drop() override
        {
            if (data)
                parent = nullptr;
            else
                suicide();
        }

        void read(size_t begin, size_t end, char* ptr, IOCtrl* io) override
        {
            if (!N && end > conf.size)
            {
                io->fail();
                unlock();
                io->unlock();
                return;
            }

            if (!N || !data && Controller::ctrl->usage(N) == Controller::Full)
            {
                fread(ptr, begin, end - begin, io);
                unlock();
            }
            else
            {
                if (!data)
                    conf(this, N);
                cachedRead<N>(begin, end, ptr, io);
            }
            io->unlock();
        }

        void write(size_t begin, size_t end, const char* ptr, IOCtrl* io) override
        {
            if (!N && end > conf.size)
                if (fwrite("0", end - 1, 1, io))
                    conf.size = end;
                else
                {
                    unlock();
                    io->unlock();
                    return;
                }

            if (!N || !data && Controller::ctrl->usage(N) == Controller::Full)
            {
                fwrite(ptr, begin, end - begin, io);
                unlock();
            }
            else
            {
                if (!data)
                    conf(this, N);
                cachedWrite<N>(begin, end, ptr, io);
            }
            io->unlock();
        }

        static void sync(IOCtrl* io)
        {
            io->unlock();
        }

        void flush(IOCtrl* io) override
        {
            if (dirty)
                if (fwrite(data, offset, effective, io))
                {
                    dirty = false;
                    unlock();
                }
        }

        void evict() override
        {
            std::cerr << "Evicting {" + std::to_string(offset) + ", " << std::to_string(offset + N) << "}\n" << std::flush;
            if (dirty)
            {
                fwrite(data, offset, effective);
                dirty = false;
                unlock();
            }
            if (data)
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

        Root root;

    public:
        static constexpr auto level = sizeof...(rest) + 1;
        static constexpr std::array<size_t, level> bits = {n, rest...};

        // Bind to the file from given path.
        explicit BTree(const std::string &path) : BTreeBase(path), root(conf, 0)
        {
            mtxUsedID.lock();
            while (usedID.count(id = rand()));
            usedID.insert(id);
            mtxUsedID.unlock();

            std::fstream file(path, std::ios_base::in | std::ios_base::binary);
            if (file.is_open())
                conf.size = file.seekg(0, std::ios_base::end).tellg();
            else
                file.open(path, std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);

            if (!file.is_open())
                root.fail();
        }

        ~BTree()
        {
            std::lock_guard<std::mutex> lck(mtxUsedID);
            usedID.erase(id);
        }

        // Issue a read request ont this file to the given controller.
        auto read(Controller& ctrl, size_t begin, size_t end, char* ptr)
        {
            auto io = new IOCtrl();
            io->lock();
            if (!ctrl.workers[id % ctrl.workers.size()].pushPending([=](){ root.read(begin, end, ptr, io); }))
                io->state.store(IOCtrl::Rejected, std::memory_order_relaxed);
            return io;
        }

        // Issue a write request ont this file to the given controller.
        auto write(Controller& ctrl, size_t begin, size_t end, const char* ptr)
        {
            auto io = new IOCtrl();
            io->lock();
            if (!ctrl.workers[id % ctrl.workers.size()].pushPending([=](){ root.write(begin, end, ptr, io); }))
                io->state.store(IOCtrl::Rejected, std::memory_order_relaxed);
            return io;
        }

        // Issue a sync request ont this file to the given controller.
        auto sync(Controller& ctrl)
        {
            auto io = new IOCtrl();
            io->lock();
            if (!ctrl.workers[id % ctrl.workers.size()].pushPending([=](){ root.flush(io); }) || !ctrl.workers[id % ctrl.workers.size()].pushPending([=](){ Root::sync(io); }))
                io->state.store(IOCtrl::Rejected, std::memory_order_relaxed);
            return io;
        }

        // Close the file.
        void close()
        {
            conf.files.clear();
        }
    };

    // Default hierarchy.
    using BTreeDefault = BTree<32, 2, 9, 9, 12>;
}
