#include "BTreeBase.hpp"

#include <unordered_set>
#include <mutex>
#include <random>

namespace tai
{
    std::unordered_set<size_t> BTreeBase::usedID;
    std::mutex BTreeBase::mtxUsedID;
    std::default_random_engine BTreeBase::rand;
}
