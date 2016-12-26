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
                child[i] = new Child(conf, offset ^ i << m);
            return child[i];
        }

        auto touch(const size_t& i, const size_t& offset)
        {
            if (!child[i])
                child[i] = new Child(conf, offset);
            return child[i];
        }

    public:
        BTreeNode(const std::shared_ptr<BTreeConfig>& conf, const size_t& offset) : BTreeNodeBase(conf, offset), child(1)
        {
        }

        BTreeNode(const Self&) = delete;

        BTreeNode(Self&& _) : BTreeNodeBase(std::forward<Self>(_)), child(std::move(_.child))
        {
            child.clear();
            child.shrink_to_fit();
        }

        auto& operator =(Self&& _) override
        {
            dirty = _.dirty;
            _.dirty = false;
            delete[] data;
            data = _.data;
            _.data = nullptr;
            conf = std::move(_.conf);
            child = std::move(_.child);
            child.clear();
            child.shrink_to_fit();
        }

        ~BTreeNode() override
        {
            evict();
        }

        void read(const size_t& begin, const size_t& end, char* const& ptr) override
        {
            if (invalid)
                return;

            if (data)
                memcpy(ptr, data + (begin & NM - 1), end - begin);
            else
            {
                if (dirty)
                {
                    const auto range = getRange(begin, end);
                    if (child.size() < range.second)
                        child.resize(range.second);

                    auto node = touch(range.first, begin & -M);
                    if (range.first == range.second)
                        Worker::push([=](){ node->read(begin, end, ptr); });
                    else
                    {
                        Worker::push([=](){ node->read(begin, (begin & -M) + M, ptr); });
                        auto suffix = ptr + (begin & M - 1 ? -begin & M - 1 : M);
                        for (auto i = range.first; ++i != range.second; suffix += M)
                        {
                            node = touch(i);
                            Worker::push([=](){ node->read(offset ^ i << m, offset ^ i + 1 << m, suffix); });
                        }
                        node = touch(range.second, end - 1 & -M);
                        Worker::push([=](){ node->read(end - 1 & -M, end, suffix); });
                    }
                }
                else if (end - begin >> nm && Controller::ctrl->usage(NM) != Controller::Full)
                {
                    (*conf)(this, NM);
                    conf->file.seekg(begin);
                    conf->file.read(data, NM);
                    memcpy(ptr, data, NM);
                    Worker::recycle(child);
                }
                else
                {
                    conf->file.seekg(begin);
                    conf->file.read(ptr, end - begin);
                }
            }
        }

        void write(const size_t& begin, const size_t& end, char* const& ptr) override
        {
            if (invalid)
                return;

            if (data)
            {
                memcpy(data + (begin & NM - 1), ptr, end - begin);
                dirty = true;
            }
            else if (end - begin >> nm)
            {
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
                Worker::recycle(child);
            }
            else
            {
                dirty = true;

                const auto range = getRange(begin, end);
                if (child.size() < range.second)
                    child.resize(range.second);

                auto node = touch(range.first, begin & -M);
                if (range.first == range.second)
                    Worker::push([=](){ node->write(begin, end, ptr); });
                else
                {
                    Worker::push([=](){ node->write(begin, (begin & -M) + M, ptr); });
                    auto suffix = ptr + (-begin & M - 1);
                    for (auto i = range.first; ++i != range.second; suffix += M)
                    {
                        node = touch(i);
                        Worker::push([=](){ node->write(begin & -NM ^ i << m, begin & -NM ^ i + 1 << m, suffix); });
                    }
                    node = touch(range.second, end - 1 & -M);
                    Worker::push([=](){ node->write(end - 1 & -M, end, suffix); });
                }
            }
        }

        void flush()
        {
            if (data && dirty)
            {
                conf->file.seekp(offset);
                conf->file.write(data, NM);
                dirty = false;
            }
        }

        void evict()
        {
            if (data)
            {
                if (dirty)
                {
                    conf->file.seekp(offset);
                    conf->file.write(data, NM);
                    dirty = false;
                }
                (*conf)(data, NM);
            }
        }

        void invalidate() override
        {
            invalid = true;
            dirty = false;
            for (auto& i : child)
                if (i)
                    i->invalidate();
        }
    };

    template<size_t n>
    class BTreeNode<n> : public BTreeNodeBase
    {
        static_assert(n <= sizeof(size_t), "B-tree node cannot be larger than the size of address space.");

    public:
        using Self = BTreeNode<n>;

        static constexpr auto N = 1 << n;

        BTreeNode(const std::shared_ptr<BTreeConfig>& conf, const size_t& offset) : BTreeNodeBase(conf, offset)
        {
        }


        BTreeNode(const Self&) = delete;

        BTreeNode(Self&& _) : BTreeNodeBase(std::forward<Self>(_))
        {
        }

        ~BTreeNode() override
        {
            evict();
        }

        auto& operator =(Self&& _) override
        {
            dirty = _.dirty;
            _.dirty = false;
            delete[] data;
            data = _.data;
            _.data = nullptr;
            conf = std::move(_.conf);
        }

        void read(const size_t& begin, const size_t& end, char* const& ptr) override
        {
            if (invalid)
                return;

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
            if (invalid)
                return;

            if (!data)
                if (Controller::ctrl->usage(N) == Controller::Full)
                {
                    conf->file.seekp(begin);
                    conf->file.write(ptr, end - begin);
                }
                else
                {
                    (*conf)(this, N);
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
                }
            if (data)
            {
                memcpy(data + (begin & N - 1), ptr, end - begin);
                dirty = true;
            }
        }

        void flush() override
        {
            if (dirty && data)
            {
                conf->file.seekp(offset);
                conf->file.write(data, N);
                dirty = false;
            }
        }

        void evict()
        {
            if (data)
            {
                if (dirty)
                {
                    conf->file.seekp(offset);
                    conf->file.write(data, N);
                    dirty = false;
                }
                (*conf)(this, N);
            }
        }

        void invalidate() override
        {
            invalid = true;
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

        explicit BTree(const std::string &path) : conf(path), root(conf)
        {
        }
    };

    using BTreeDefault = BTree<32, 2, 9, 9, 12>;
}
