#include "BTreeBase.hpp"

#include <unordered_set>
#include <mutex>
#include <random>
#include <chrono>

#include <boost/lockfree/queue.hpp>

namespace tai
{
    std::unordered_set<size_t> BTreeBase::usedID;
    std::mutex BTreeBase::mtxUsedID;
    boost::lockfree::queue<BTreeConfig*> BTreeBase::confPool(1);
    std::default_random_engine BTreeBase::rand(std::chrono::high_resolution_clock::now().time_since_epoch().count());
}
