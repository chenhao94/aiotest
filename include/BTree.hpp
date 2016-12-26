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

        std::vector<std::shared_ptr<Child>> child;

    private:
        void cacheQueue(std::shared_ptr<Child>& node)
        {
            if (!node->flushing)
            {
                node->flushing = true;
                Controller::ctrl->dirty.push(new Controller::SafeNode(node));
            }
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
            {
                dirty = false;
                (*conf)(data, NM);
                return;
            }

            if (data)
                memcpy(ptr, data + (begin & NM - 1), end - begin);
            else if (dirty)
            {
                const std::pair<size_t, size_t> range = {begin >> m & N - 1, end - 1 >> m & N - 1};

                if (child.size() < range.second)
                    child.resize(range.second);

                auto& node = child[range.first];
                if (!node)
                    node = new Child(conf, begin & -M);

                if (range.first == range.second)
                    Worker::push([=](){ node->read(begin, end, ptr); });
                else
                {
                    Worker::push([=](){ node->read(begin, (begin & -M) + M, ptr); });
                    auto suffix = ptr + (-begin & M - 1);
                    for (auto i = range.first; ++i != range.second; suffix += M)
                    {
                        if (!(node = child[i]))
                            child[i] = new Child(conf, begin & -NM ^ i + 1 << m);
                        Worker::push([=](){ node->read(begin & -NM ^ i << m, begin & -NM ^ i + 1 << m, suffix); });
                    }
                    if (!(node = child[range.second]))
                        node = new Child(conf, end - 1 & -M);
                    Worker::push([=](){ node->read(end - 1 & -M, end, suffix); });
                }
            }
            else
            {
                conf->file.seekg(begin);
                conf->file.read(ptr, end - begin);
            }
            if (end - begin >> nm && (data || Controller::ctrl->usage(NM) != Controller::Full))
            {
                memcpy(data ? data : (data = (*conf)(NM)), ptr, NM);
                for (auto& i : child)
                    if (i)
                        Worker::remove(std::move(i));
                child.clear();
            }
        }

        void write(const size_t& begin, const size_t& end, char* const& ptr) override
        {
            if (invalid)
            {
                dirty = false;
                (*conf)(data, NM);
                return;
            }

            if (data || end - begin >> nm)
            {
                if (data || Controller::ctrl->usage(NM) != Controller::Full)
                    memcpy(data ? data : (data = (*conf)(NM)), ptr, NM);
                else
                {
                    conf->file.seekp(begin);
                    conf->file.write(ptr, end - begin);
                }
                dirty = true;
                for (auto& i : child)
                    if (i)
                        Worker::remove(std::move(i));
                child.clear();
            }
            else
            {
                dirty = true;
                const std::pair<size_t, size_t> range = {begin >> m & N - 1, end - 1 >> m & N - 1};

                if (child.size() < range.second)
                    child.resize(range.second);

                auto& node = child[range.first];
                if (node)
                    cacheQueue(node);
                else
                    node = new Child(conf, begin & -M);

                if (range.first == range.second)
                    Worker::push([=](){ node->write(begin, end, ptr); });
                else
                {
                    Worker::push([=](){ node->write(begin, (begin & -M) + M, ptr); });
                    auto suffix = ptr + (-begin & M - 1);
                    for (auto i = range.first; ++i != range.second; suffix += M)
                    {
                        if (!(node = child[i]))
                            child[i] = new Child(conf, begin & -NM ^ i + 1 << m);
                        else
                            cacheQueue(node);
                        Worker::push([=](){ node->write(begin & -NM ^ i << m, begin & -NM ^ i + 1 << m, suffix); });
                    }
                    if (!(node = child[range.second]))
                        node = new Child(conf, end - 1 & -M);
                    else
                        cacheQueue(node);
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
                }
                (*conf)(data, NM);
                dirty = false;
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
            {
                dirty = false;
                (*conf)(data, N);
                return;
            }

            if (!data && Controller::ctrl->usage(N) != Controller::Full)
            {
                conf->file.seekg(offset);
                conf->file.read(data = (*conf)(N), N);
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
            {
                dirty = false;
                (*conf)(data, N);
                return;
            }

            if (!data)
                if (Controller::ctrl->usage(N) == Controller::Full)
                {
                    conf->file.seekp(begin);
                    conf->file.write(ptr, end - begin);
                }
                else
                {
                    data = (*conf)(N);
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
                }
                (*conf)(data, N);
                dirty = false;
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
