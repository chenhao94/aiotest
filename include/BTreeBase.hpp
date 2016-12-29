#pragma once

#include <unordered_set>
#include <mutex>
#include <random>

#include "BTreeNodeBase.hpp"

namespace tai
{
    class BTreeBase
    {
    public:
        // Reuseable global ID for worker association.
        static std::unordered_set<size_t> usedID;
        static std::mutex mtxUsedID;
        size_t id = 0;

        // Random engine.
        static std::default_random_engine rand;

        // Configuration.
        BTreeConfig conf;

        BTreeBase(const std::string& path) : conf(path)
        {
        }
    };
}
