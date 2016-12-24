#pragma once

#include <string>
#include <memory>
#include <fstream>
#include <algorithm>
#include <vector>
#include <functional>

#include "TLQ.hpp"
#include "Worker.hpp"

namespace tai
{
    class TLQ;
    class Worker;

    class BTreeConfig
    {
    public:
        const std::string path = "";
        std::fstream file;

        BTreeConfig(const std::string &path) : path(path), file(path)
        {
        }

        ~BTreeConfig()
        {
            if (file)
                file.close();
        }

        operator bool()
        {
            return file.is_open();
        }
    };

    class BTreeNodeBase
    {
    public:
        using Self = BTreeNodeBase;

        virtual void read(const size_t& begin, const size_t& end, char* const& ptr) = 0;
        virtual void write(const size_t& begin, const size_t& end, char* const& ptr) = 0;

        BTreeNodeBase(const std::shared_ptr<BTreeConfig>& conf) : conf(conf)
        {
        }

        BTreeNodeBase(const Self& _) : dirty(_.dirty), conf(_.conf)
        {
        }

        BTreeNodeBase(Self&& _) : dirty(_.dirty), data(_.data), conf(std::move(_.conf))
        {
            _.data = nullptr;
        }

        ~BTreeNodeBase()
        {
            delete[] data;
        }

        virtual Self& operator =(const Self& _) = 0;
        virtual Self& operator =(Self&& _) = 0;

    protected:
        bool dirty = false;
        char* data = nullptr;

        std::shared_ptr<BTreeConfig> conf = nullptr;
    };

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
                node = new Child(conf);

            if (range.first == range.second)
                Worker::push([=](){ node->*op(begin, end, ptr); });
            else
            {
                Worker::push([=](){ node->*op(begin, (begin & -M) + M, ptr); });
                auto suffix = ptr + (-begin & M - 1);
                for (auto i = range.first; ++i != range.second; suffix += M)
                {
                    if (!(node = child[i]))
                        child[i] = new Child(conf);
                    Worker::push([=](){ node->*op((begin & -NM) + (i << m), (begin & -NM) + (i << m) + M, suffix); });
                }
                if (!(node = child[range.second]))
                    node = new Child(conf);
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

        std::vector<std::shared_ptr<Child>> child;

        BTreeNode(const std::shared_ptr<BTreeConfig>& conf) : BTreeNodeBase(conf), child(1)
        {
        }

        BTreeNode(const Self& _) : BTreeNodeBase(_)
        {
            memcpy(data ? data : (data = new char[NM]), _.data, NM);
            child.clear();
            child.reserve(_.child.capacity());
            for (auto& i : _.child)
                child.emplace_back(new Child(*i));
        }

        BTreeNode(Self&& _) : BTreeNodeBase(std::forward<Self>(_)), child(std::move(_.child))
        {
            child.clear();
            child.shrink_to_fit();
        }

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

        void read(const size_t& begin, const size_t& end, char* const& ptr) override
        {
            if (dirty)
                rw(&Self::read, begin, end, ptr);
            else
            {
                conf->file.seekg(begin);
                conf->file.read(ptr, end - begin);
            }
        }

        void write(const size_t& begin, const size_t& end, char* const& ptr) override
        {
            if (end - begin >> nm)
            {
                if (data == nullptr)
                    data = new char[NM];
                memcpy(data, ptr, NM);
                dirty = true;
                child.clear();
            }
            else
            {
                dirty = true;
                rw(&Self::write, begin, end, ptr);
            }
        }
    };

    template<size_t n>
    class BTreeNode<n> : public BTreeNodeBase
    {
        static_assert(n <= sizeof(size_t), "B-tree node cannot be larger than the size of address space.");

    public:
        using Self = BTreeNode<n>;

        static constexpr auto N = 1 << n;

        BTreeNode(const std::shared_ptr<BTreeConfig>& conf) : BTreeNodeBase(conf)
        {
        }

        BTreeNode(const Self& _) : BTreeNodeBase(_)
        {
            memcpy(data ? data : (data = new char[N]), _.data, N);
        }

        BTreeNode(Self&& _) : BTreeNodeBase(std::forward<Self>(_))
        {
        }

        auto& operator =(const Self& _) override
        {
            dirty = _.dirty;
            memcpy(data ? data : (data = new char[N]), _.data, N);
            conf = _.conf;
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
            if (dirty)
                memcpy(ptr, data + begin, end - begin);
            else
            {
                conf->file.seekg(begin);
                conf->file.read(ptr, end - begin);
            }
        }

        void write(const size_t& begin, const size_t& end, char* const& ptr) override
        {
            if (!dirty)
            {
                if (!data)
                    data = new char[N];
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
    };

    template<size_t... n>
    class BTree
    {
        static_assert((n + ...) == sizeof(size_t), "B-tree must cover the entire address space.");

        std::atomic<size_t> count = {0};
        BTreeConfig conf;
        BTreeNode<n...> root;

    public:
        static constexpr auto level = sizeof...(n);
        static constexpr std::array<size_t, level> bits = {n...};

        explicit BTree(const std::string &path) : conf(path), root(conf)
        {
        }
    };

    using BTreeDefault = BTree<34, 9, 9, 12>;
}
