#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <string>
#include <ctime>
#include <chrono>
#include <thread>
#include <vector>
#include <limits>
#include <numeric>
#include <functional>
#include <new>

#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <aio.h>

#ifdef __linux__
#include <libaio.h>
#endif

#include "tai.hpp"
#include "aio.hpp"
#include "./testlib/iotest.hpp"

#define unlikely(x)     __builtin_expect((x),0)

using namespace std;
using namespace chrono;
using namespace this_thread;

int main(int argc, char* argv[])
{
    processArgs(argc, argv);
    srand(time(nullptr));
    system("sync");

    vector<thread> threads;
    auto begin = high_resolution_clock::now();
    for (size_t i = 0; i < thread_num; ++i)
    {
        auto rw = getInstance(testType);
        threads.emplace_back(thread([&](){rw->run(i);}));
    }
    for (auto& t : threads)
        t.join();

    auto time = duration_cast<nanoseconds>(high_resolution_clock::now() - begin).count();
    if (testType == 4)
        tai::aio_end();
    tai::Log::log(testname[testType], " random ", wlname[workload], ": ", time / 1e9, " s in total, ",
            IO_ROUND * (int(workload == 2) + 1), " ops/thread, ",
            READ_SIZE >> 10, " KB/read, " , WRITE_SIZE >> 10, " KB/write, ", 
            thread_num, " threads, ", 1e9 * IO_ROUND * (int(workload == 2) + 1) * thread_num / time, " iops");
    if (testType == 4)
    {
        auto rt = tai::Log::run_time.load() / 1e9;
        auto st = tai::Log::steal_time.load() / 1e9;
        auto bt = tai::Log::barrier_time.load() / 1e9;
        tai::Log::log("Total run time: " , rt, " s, steal time: ", st, " s, barrier time: ", bt, " s");
    }

    return 0;
}
