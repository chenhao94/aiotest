#pragma once

#include <string>
#include <fstream>
#include <functional>

namespace tai
{
    class BTreeConfig
    {
    public:
        const std::string path = "";
        std::fstream file;

        BTreeConfig(const std::string& path);
        ~BTreeConfig();

        operator bool();

        auto operator()(const size_t& size);
        void operator()(char*& ptr, const size_t& size);
    };

    class BTreeNodeBase
    {
    public:
        bool flushing = false;

        using Self = BTreeNodeBase;

        virtual void read(const size_t& begin, const size_t& end, char* const& ptr) = 0;
        virtual void write(const size_t& begin, const size_t& end, char* const& ptr) = 0;
        virtual void flush() = 0;
        virtual void evict() = 0;
        virtual void invalidate() = 0;

        BTreeNodeBase(const std::shared_ptr<BTreeConfig>& conf, const size_t& offset);
        BTreeNodeBase(const Self&) = delete;
        BTreeNodeBase(Self&& _);
        virtual ~BTreeNodeBase() = 0;

        virtual Self& operator =(const Self&) = delete;
        virtual Self& operator =(Self&& _) = 0;

    protected:
        bool dirty = false;
        char* data = nullptr;
        size_t offset = 0;
        bool invalid = false;

        std::shared_ptr<BTreeConfig> conf = nullptr;
    };
}
