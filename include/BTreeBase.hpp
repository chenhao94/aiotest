#pragma once

#include <unordered_set>
#include <mutex>
#include <random>

#include <boost/lockfree/queue.hpp>

#include "BTreeNodeBase.hpp"
#include "Controller.hpp"
#include "IOCtrl.hpp"

namespace tai
{
    class BTreeBase
    {
    public:
        // Reuseable global ID for worker association.
        static std::unordered_set<size_t> usedID;
        static volatile size_t IDCount;
        static std::mutex mtxUsedID;
        static boost::lockfree::queue<BTreeConfig*> confPool;
        size_t id = 0;

        // Random engine.
        static std::default_random_engine rand;

        // Configuration.
        BTreeConfig& conf;

        BTreeBase(const std::string& path) : conf(*new BTreeConfig(path))
        {
            confPool.push(&conf);
        }

        virtual ~BTreeBase()
        {
        }

        auto failed() const
        {
            return conf.failed.load(std::memory_order_relaxed);
        }

        virtual IOCtrl* read(Controller& ctrl, size_t pos, size_t len, char* ptr) = 0;
        virtual IOCtrl* readsome(Controller& ctrl, size_t pos, size_t len, char* ptr) = 0;
        virtual IOCtrl* write(Controller& ctrl, size_t pos, size_t len, const char* ptr) = 0;
        virtual IOCtrl* sync(Controller& ctrl) = 0;
        virtual IOCtrl* detach(Controller& ctrl) = 0;
        virtual void close() = 0;
    };
}
