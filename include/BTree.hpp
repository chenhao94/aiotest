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
    class TLQ;
    class Controller;
    class Worker;

    template<size_t n, size_t... rest>
    class BTreeNode : public BTreeNodeBase
    {
        static_assert(n <= sizeof(size_t), "B-tree node cannot be larger than the size of address space.");
        static_assert((n + ... + rest) <= sizeof(size_t), "B-tree node cannot be larger than the size of address space.");

        template<typename Fn>
        void rw(Fn op, const size_t& begin, const size_t& end, char* const& ptr)
        {
            const std::pair<size_t, size_t> range = {begin >> m & N - 1, end - 1 >> m & N - 1};

            if (child.size() < range.second)
                child.resize(range.second);

            auto& node = child[range.first];
            if (!node)
                node = new Child(conf, begin & -M);

            if (range.first == range.second)
                Worker::push([=](){ node->*op(begin, end, ptr); });
            else
            {
                Worker::push([=](){ node->*op(begin, (begin & -M) + M, ptr); });
                auto suffix = ptr + (-begin & M - 1);
                for (auto i = range.first; ++i != range.second; suffix += M)
                {
                    if (!(node = child[i]))
                        child[i] = new Child(conf, begin & -NM ^ i + 1 << m);
                    Worker::push([=](){ node->*op(begin & -NM ^ i << m, begin & -NM ^ i + 1 << m, suffix); });
                }
                if (!(node = child[range.second]))
                    node = new Child(conf, end - 1 & -M);
                Worker::push([=](){ node->*op(end - 1 & -M, end, suffix); });
            }
        }

    public:
        using Self = BTreeNode<n, rest...>;
        using Child = BTreeNode<rest...>;

        static constexpr auto N = 1 << n;
        static constexpr auto m = (rest + ...);
        static constexpr auto M = 1 << m;
        static constexpr auto nm = n + m;
        static constexpr auto NM = N << m;

        std::vector<std::unique_ptr<Child>> child;

        BTreeNode(const std::shared_ptr<BTreeConfig>& conf, const size_t& offset) : BTreeNodeBase(conf, offset), child(1)
        {
        }

        BTreeNode(const Self&) = delete;
        /*
        BTreeNode(const Self& _) : BTreeNodeBase(_)
        {
            memcpy(data ? data : (data = new char[NM]), _.data, NM);
            child.clear();
            child.reserve(_.child.capacity());
            for (auto& i : _.child)
                child.emplace_back(new Child(*i));
        }
        */

        BTreeNode(Self&& _) : BTreeNodeBase(std::forward<Self>(_)), child(std::move(_.child))
        {
            child.clear();
            child.shrink_to_fit();
        }

        /*
        auto& operator =(const Self& _) override
        {
            dirty = _.dirty;
            memcpy(data ? data : (data = new char[NM]), _.data, NM);
            conf = _.conf;
            child.clear();
            child.reserve(_.child.capacity());
            for (auto& i : _.child)
                child.emplace_back(new Child(*i));
        }
        */

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
            if (data)
                (*conf)(data, NM);
        }

        void read(const size_t& begin, const size_t& end, char* const& ptr) override
        {
            if (invalid)
            {
                dirty = false;
                if (data)
                    (*conf)(data, NM);
                return;
            }

            if (data)
                memcpy(ptr, data + (begin & NM - 1), end - begin);
            else if (dirty)
                rw(&Self::read, begin, end, ptr);
            else
            {
                conf->file.seekg(begin);
                conf->file.read(ptr, end - begin);
            }
            if (end - begin >> nm)
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
                if (data)
                    (*conf)(data, NM);
                return;
            }

            if (data || end - begin >> nm)
            {
                memcpy(data ? data : (data = (*conf)(NM)), ptr, NM);
                dirty = true;
                for (auto& i : child)
                    if (i)
                        Worker::remove(std::move(i));
                child.clear();
            }
            else
            {
                dirty = true;
                rw(&Self::write, begin, end, ptr);
            }
        }

        void flush()
        {
            if (data && dirty)
            {
                conf->file.seekp(offset);
                conf->file.write(data, NM);
                (*conf)(data, NM);
            }
        }

        void invalidate() override
        {
            invalid = true;
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
        /*
        BTreeNode(const Self& _) : BTreeNodeBase(_)
        {
            memcpy(data ? data : (data = new char[N]), _.data, N);
        }
        */

        BTreeNode(Self&& _) : BTreeNodeBase(std::forward<Self>(_))
        {
        }

        /*
        auto& operator =(const Self& _) override
        {
            dirty = _.dirty;
            memcpy(data ? data : (data = new char[N]), _.data, N);
            conf = _.conf;
        }
        */

        ~BTreeNode() override
        {
            if (data)
                (*conf)(data, N);
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
                if (data)
                    (*conf)(data, N);
                return;
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
                if (data)
                    (*conf)(data, N);
                return;
            }

            if (!data)
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
            memcpy(data + (begin & N - 1), ptr, end - begin);
            dirty = true;
        }

        void flush() override
        {
            if (dirty && data)
            {
                conf->file.seekp(offset);
                conf->file.write(data, N);
                (*conf)(data, N);
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
