#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <string>
#include <thread>
#include <memory>
#include <chrono>
#include <random>

#include "tai.hpp"

int main()
{
    using namespace std;
    using namespace chrono;
    using namespace this_thread;

    using namespace tai;

    default_random_engine rand;

    Log::log("Creating B-tree...");
    // BTreeDefault bt("tmp");
    BTreeTrivial bt("tmp");

    Log::log("Creating Controller...");
    Controller ctrl(1 << 28, 1 << 30);

    Log::log("Writing...");

    const auto size = (size_t)1 << 30;
    const auto bs = (size_t)1 << 12;
    const auto n = (size_t)1 << 20;

    uniform_int_distribution<size_t> dist(0, size - bs);

    const auto start = high_resolution_clock::now();
    string data(bs, '0');
    for (auto& i : data)
        i += rand();

    for (auto i = n; i--; bt.write(ctrl, dist(rand) % size, data.size(), data.data()));

    {
        unique_ptr<IOCtrl> io(bt.sync(ctrl));
        for (; (*io)() == IOCtrl::Running; sleep_for(seconds(1)))
            Log::log("Still running...");
        Log::log(to_cstring((*io)()));
    }

    Log::log("Performance: ", (size_t)round(n * 1e9 / duration_cast<nanoseconds>(high_resolution_clock::now() - start).count()), " iops.");
    Log::log("Complete!");
    return 0;
}
