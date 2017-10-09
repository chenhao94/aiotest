#include "BTreeBase.hpp"

#include <unordered_set>
#include <mutex>
#include <random>
#include <chrono>

namespace tai
{
    std::unordered_set<size_t> BTreeBase::usedID;
    std::mutex BTreeBase::mtxUsedID;
    std::default_random_engine BTreeBase::rand(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    volatile size_t BTreeBase::IDCount = 0;
}
