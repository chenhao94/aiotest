#include <iostream>
#include <fstream>
#include <cstdio>
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

int main(int argc, char* argv[])
{
    using namespace std;
    using namespace chrono;
    using namespace this_thread;

    using namespace tai;

    const auto size = (size_t)1 << 30;
    const auto bs = (size_t)1 << 12;
    const auto n = (size_t)1 << 15;

    default_random_engine rand;
    uniform_int_distribution<size_t> dist(0, size - bs);

    string data(bs, '0');
    for (auto& i : data)
        i += rand();

    if (argc <= 1 || atoll(argv[1]) & 1)
    {
        Log::log("Creating file for sync I/O...");

        fstream file("tmp-sync", ios_base::in | ios_base::out | ios_base::binary);

        if (!file.is_open())
            file.open("tmp-sync", ios_base::in | ios_base::out | ios_base::trunc | ios_base::binary);

        Log::log("Writing...");

        const auto start = high_resolution_clock::now();

        for (auto i = n; i--; file.seekp(dist(rand) % size).write(data.data(), data.size()))
            if (i % (n / 100) == 0)
                Log::log("\t", (n - i - 1) * 100 / n, "% Performance: ", (size_t)round((n - i - 1) * 1e9 / duration_cast<nanoseconds>(high_resolution_clock::now() - start).count()), " iops.");

        file.flush();

        Log::log("Performance: ", (size_t)round(n * 1e9 / duration_cast<nanoseconds>(high_resolution_clock::now() - start).count()), " iops.");
    }

    if (argc <= 1 || atoll(argv[1]) & 2)
    {
        Log::log("Creating B-tree...");
        // BTreeDefault bt("tmp");
        BTreeTrivial bt("tmp-tai");

        Log::log("Creating Controller...");
        // Controller ctrl(1 << 28, 1 << 30, 1);
        Controller ctrl(1 << 28, 1 << 30);

        Log::log("Writing...");

        const auto start = high_resolution_clock::now();

        for (auto i = n; i--; bt.write(ctrl, dist(rand) % size, data.size(), data.data()))
            if (i % (n / 100) == 0)
                Log::log("\t", (n - i - 1) * 100 / n, "% Performance: ", (size_t)round((n - i - 1) * 1e9 / duration_cast<nanoseconds>(high_resolution_clock::now() - start).count()), " iops.");

        {
            unique_ptr<IOCtrl> io(bt.sync(ctrl));
            for (; (*io)() == IOCtrl::Running; sleep_for(seconds(1)))
                Log::log("Still running...");
            Log::log(to_cstring((*io)()));
        }

        Log::log("Performance: ", (size_t)round(n * 1e9 / duration_cast<nanoseconds>(high_resolution_clock::now() - start).count()), " iops.");
    }

    Log::log("Complete!");
    return 0;
}
