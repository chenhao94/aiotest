#pragma once

#include <string>
#include <fstream>
#include <functional>

namespace tai
{
    class BTreeNodeBase;

    class BTreeConfig
    {
    public:
        const std::string path = "";
        std::fstream file;

        BTreeConfig(const std::string& path);
        ~BTreeConfig();

        operator bool();

        void operator()(BTreeNodeBase* const node, const size_t& size);
    };

    class BTreeNodeBase
    {
    public:
        friend class BTreeConfig;

        using Self = BTreeNodeBase;

        virtual void read(const size_t& begin, const size_t& end, char* const& ptr) = 0;
        virtual void write(const size_t& begin, const size_t& end, char* const& ptr) = 0;
        virtual void flush() = 0;
        virtual void evict() = 0;
        bool valid();

        BTreeNodeBase(const std::shared_ptr<BTreeConfig>& conf, const size_t& offset, BTreeNodeBase* const parent = nullptr);
        virtual ~BTreeNodeBase() = 0;

    protected:
        std::atomic<size_t> lck = {0};
        BTreeNodeBase* parent = nullptr;
        char* data = nullptr;
        size_t offset = 0;
        bool dirty = false;

        std::shared_ptr<BTreeConfig> conf = nullptr;

        size_t lock();
        void lock(const size_t& num);
        void unlock();
    };
}
