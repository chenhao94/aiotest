#pragma once

#include <string>
#include <memory>
#include <fstream>
#include <algorithm>
#include <vector>
#include <functional>

#include <boost/lockfree/queue.hpp>

#include "BTreeNodeBase.hpp"
#include "TLQ.hpp"
#include "Controller.hpp"
#include "Worker.hpp"

namespace tai
{
    template<size_t n, size_t... rest>
    class BTreeNode : public BTreeNodeBase
    {
        static_assert(n <= sizeof(size_t), "B-tree node cannot be larger than the size of address space.");
        static_assert((n + ... + rest) <= sizeof(size_t), "B-tree node cannot be larger than the size of address space.");

    public:
        using Self = BTreeNode<n, rest...>;
        using Child = BTreeNode<rest...>;

        static constexpr auto N = 1 << n;
        static constexpr auto m = (rest + ...);
        static constexpr auto M = 1 << m;
        static constexpr auto nm = n + m;
        static constexpr auto NM = N << m;

        std::vector<Child*> child;

    private:
        auto getRange(const size_t& begin, const size_t& end) const
        {
            return std::pair<size_t, size_t>{begin >> m & N - 1, end - 1 >> m & N - 1};
        }

        auto touch(const size_t& i)
        {
            if (!child[i])
                child[i] = new Child(conf, offset ^ i << m, this);
            return child[i];
        }

        auto touch(const size_t& i, const size_t& offset)
        {
            if (!child[i])
                child[i] = new Child(conf, offset, this);
            return child[i];
        }

    public:
        BTreeNode(const std::shared_ptr<BTreeConfig>& conf, const size_t& offset, BTreeNode* const parent = nullptr) : BTreeNodeBase(conf, offset, parent), child(1)
        {
        }

        ~BTreeNode() override
        {
            evict();
        }

        void merge(char* ptr)
        {
            if (data)
            {
                memcpy(data, ptr, NM);
                parent = nullptr;
            }
            else
            {
                for (size_t i = 0; i < child.size(); ++i)
                {
                    if (child[i])
                        child[i]->merge(ptr);
                    else
                    {
                        conf->file.seekg(offset ^ i << m);
                        conf->file.read(ptr, M);
                    }
                    ptr += M;
                }
                if (child.size() < N)
                {
                    conf->file.seekg(offset ^ child.size() << m);
                    conf->file.read(ptr, N - child.size() << m);
                }

                delete this;
            }
        }

        void drop()
        {
            if (data)
                parent = nullptr;
            else
            {
                for (auto& i : child)
                    if (i)
                        i->drop();
                delete this;
            }
        }

    private:
        void merge()
        {
            (*conf)(this, NM);
            for (size_t i = 0; i < child.size(); ++i)
                if (child[i])
                    child[i]->merge(data + i << m);
                else
                {
                    conf->file.seekg(offset ^ i << m);
                    conf->file.read(data + (i << m), M);
                }
            if (child.size() < N)
            {
                conf->file.seekg(offset ^ child.size() << m);
                conf->file.read(data + (child.size() << m), N - child.size() << m);
            }
            child.clear();
        }

        void dropChild()
        {
            for (auto& i : child)
                if (i)
                    i->drop();
            child.clear();
        }

    public:
        void read(const size_t& begin, const size_t& end, char* const& ptr) override
        {
            if (data)
                memcpy(ptr, data + (begin & NM - 1), end - begin);
            else if (lck.load(std::memory_order_relaxed) || end - begin < NM)
            {
                const auto range = getRange(begin, end);
                lck.fetch_add(range.second - range.begin + 1, std::memory_order_relaxed);
                if (child.size() < range.second)
                    child.resize(range.second);

                auto node = touch(range.first, begin & -M);
                if (range.first == range.second)
                    Worker::pushWait([=](){ node->read(begin, end, ptr); });
                else
                {
                    Worker::pushWait([=](){ node->read(begin, (begin & -M) + M, ptr); });
                    auto suffix = ptr + (begin & M - 1 ? -begin & M - 1 : M);
                    for (auto i = range.first; ++i != range.second; suffix += M)
                    {
                        node = touch(i);
                        Worker::pushWait([=](){ node->read(offset ^ i << m, offset ^ i + 1 << m, suffix); });
                    }
                    node = touch(range.second, end - 1 & -M);
                    Worker::pushWait([=](){ node->read(end - 1 & -M, end, suffix); });
                }
            }
            else if (Controller::ctrl->usage(NM) != Controller::Full)
            {
                merge();
                memcpy(ptr, data, NM);
            }
            else
            {
                conf->file.seekg(begin);
                conf->file.read(ptr, NM);
            }
        }

        void write(const size_t& begin, const size_t& end, char* const& ptr) override
        {
            if (data)
            {
                memcpy(data + (begin & NM - 1), ptr, end - begin);
                if (!dirty)
                    if (end - begin > NM >> 1)
                        dirty = true;
                    else
                    {
                        conf->file.seekp(begin);
                        conf->file.write(ptr, end - begin);
                    }
            }
            else if (lck.load(std::memory_order_relaxed) || end - begin < NM)
            {
                const auto range = getRange(begin, end);
                lck.fetch_add(range.second - range.begin + 1, std::memory_order_relaxed);
                if (child.size() < range.second)
                    child.resize(range.second);

                auto node = touch(range.first, begin & -M);
                if (range.first == range.second)
                    Worker::pushWait([=](){ node->write(begin, end, ptr); });
                else
                {
                    Worker::pushWait([=](){ node->write(begin, (begin & -M) + M, ptr); });
                    auto suffix = ptr + (-begin & M - 1);
                    for (auto i = range.first; ++i != range.second; suffix += M)
                    {
                        node = touch(i);
                        Worker::pushWait([=](){ node->write(begin & -NM ^ i << m, begin & -NM ^ i + 1 << m, suffix); });
                    }
                    node = touch(range.second, end - 1 & -M);
                    Worker::pushWait([=](){ node->write(end - 1 & -M, end, suffix); });
                }
            }
            else
            {
                dropChild();
                if (Controller::ctrl->usage(NM) != Controller::Full)
                {
                    (*conf)(this, NM);
                    memcpy(data, ptr, NM);
                    dirty = true;
                }
                else
                {
                    conf->file.seekp(begin);
                    conf->file.write(ptr, end - begin);
                }
            }
        }

        void flush()
        {
            if (dirty)
            {
                conf->file.seekp(offset);
                conf->file.write(data, NM);
                dirty = false;
            }
        }

        void evict()
        {
            flush();
            if (data)
                (*conf)(data, NM);
        }
    };

    template<size_t n>
    class BTreeNode<n> : public BTreeNodeBase
    {
        static_assert(n <= sizeof(size_t), "B-tree node cannot be larger than the size of address space.");

    public:
        using Self = BTreeNode<n>;

        static constexpr auto N = 1 << n;

        BTreeNode(const std::shared_ptr<BTreeConfig>& conf, const size_t& offset, BTreeNode* const parent = nullptr) : BTreeNodeBase(conf, offset, parent)
        {
        }

        ~BTreeNode() override
        {
            evict();
        }

        void merge(char* const& ptr)
        {
            if (data)
            {
                memcpy(data, ptr, N);
                parent = nullptr;
            }
            else
            {
                conf->file.seekg(offset);
                conf->file.read(ptr, N);
            }
        }

        void drop()
        {
            if (data)
                parent = nullptr;
            else
                delete this;
        }

        void read(const size_t& begin, const size_t& end, char* const& ptr) override
        {
            if (!data && Controller::ctrl->usage(N) != Controller::Full)
            {
                (*conf)(this, N);
                conf->file.seekg(offset);
                conf->file.read(data, N);
            }
            if (data)
                memcpy(ptr, data + begin, end - begin);
            else
            {
                conf->file.seekg(begin);
                conf->file.read(ptr, end - begin);
            }
        }

        void write(const size_t& begin, const size_t& end, char* const& ptr) override
        {
            if (dirty)
                memcpy(data + (begin & N - 1), ptr, end - begin);
            else if (data)
            {
                memcpy(data + (begin & N - 1), ptr, end - begin);
                dirty = true;
            }
            else if (Controller::ctrl->usage(N) != Controller::Full)
            {
                (*conf)(this, N);
                memcpy(data + (begin & N - 1), ptr, end - begin);
                if (begin & N)
                {
                    conf->file.seekg(begin);
                    conf->file.read(data, begin & N - 1);
                }
                if (end & N)
                {
                    conf->file.seekg(end);
                    conf->file.read(data + (end & N - 1), -end & N - 1);
                }
                dirty = true;
            }
            else
            {
                conf->file.seekp(begin);
                conf->file.write(ptr, end - begin);
            }
        }

        void flush() override
        {
            if (dirty)
            {
                conf->file.seekp(offset);
                conf->file.write(data, N);
                dirty = false;
            }
        }

        void evict()
        {
            flush();
            if (data)
                (*conf)(this, N);
        }
    };

    template<size_t n, size_t... rest>
    class BTree
    {
        static_assert((n + ... + rest) == sizeof(size_t), "B-tree must cover the entire address space.");

        std::atomic<size_t> count = {0};
        BTreeConfig conf;
        BTreeNode<n, rest...> root;

    public:
        static constexpr auto level = sizeof...(rest) + 1;
        static constexpr std::array<size_t, level> bits = {n, rest...};

        explicit BTree(const std::string &path) : conf(path), root(conf, 0)
        {
        }
    };

    using BTreeDefault = BTree<32, 2, 9, 9, 12>;
}
