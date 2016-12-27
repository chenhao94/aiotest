#pragma once

#include <string>
#include <fstream>
#include <vector>
#include <functional>

#include "IOCtrl.hpp"

namespace tai
{
    class BTreeNodeBase;

    class BTreeConfig
    {
    public:
        const std::string path = "";
        std::vector<std::fstream> files;

        BTreeConfig(const std::string& path);
        ~BTreeConfig();

        void operator()(BTreeNodeBase* const node, const size_t& size);
    };

    class BTreeNodeBase
    {
    public:
        friend class BTreeConfig;

        using Self = BTreeNodeBase;

        virtual void read(const size_t& begin, const size_t& end, char* const& ptr, IOCtrl* const& io) = 0;
        virtual void write(const size_t& begin, const size_t& end, char* const& ptr, IOCtrl* const& io) = 0;
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

        void fread(char* const& buf, const size_t& pos, const size_t&len);
        void fwrite(char* const& buf, const size_t& pos, const size_t&len);

        size_t locked();
        void lock(const size_t& num = 1);
        void unlock();
        void unlock(const size_t& num);
    };
}
