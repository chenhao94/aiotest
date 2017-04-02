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
    if (argc != 10)
    {
        cerr << "Need arguments for thread number, type of IO to test and workload type" << endl;
        exit(-1);
    }
    [&](vector<size_t*> _){ for (auto i = _.size(); i--; *_[i] = stoll(argv[i + 1])); }({
            &thread_num,
            &testType,
            &workload,
            &READ_SIZE,
            &WRITE_SIZE,
            });
    [&](vector<size_t*> _){ for (auto i = _.size(); i--; *_[i] = 1ll << stoll(argv[i + 6])); }({
            &FILE_SIZE,
            &IO_ROUND,
            &SYNC_RATE,
            &WAIT_RATE
            });

    READ_SIZE = READ_SIZE << 10;
    WRITE_SIZE = WRITE_SIZE << 10;

    tai::Log::log("thread number: ", thread_num);
    srand(time(nullptr));
    system("sync");

    string testname[] = {"Blocking IO", "Blocking IO (O_DIRECT | O_SYNC)", "Async IO", "LibAIO", "TAI"};
    string wlname[] = {"read", "write", "read&write"};
    auto begin = high_resolution_clock::now();

    tai::Log::log(testname[testType]);
    vector<thread> threads;
    switch (testType)
    {
    case 0:
    case 1:
        for (size_t i = 0; i < thread_num; ++i)
            threads.emplace_back(thread(BlockingWrite::startEntry, i, -testType & (
                            #ifdef __linux__
                            O_DIRECT |
                            #endif
                            O_SYNC)));
        break;
    case 2:
        for (size_t i = 0; i < thread_num; ++i)
            threads.emplace_back(thread(AIOWrite::startEntry, i));
        break;
    case 3:
        #ifdef __linux__
        io_setup(1048576, &LibAIOWrite::io_cxt);
        #endif
        for (size_t i = 0; i < thread_num; ++i)
            threads.emplace_back(thread(LibAIOWrite::startEntry, i));
        break;
    case 4:
        tai::aio_init();
        for (size_t i = 0; i < thread_num; ++i)
            threads.emplace_back(thread(TAIWrite::startEntry, i));
        break;
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
