#include <cstdlib>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <string>
#include <chrono>
#include <thread>
#include <vector>

#include "iotest.hpp"

int main(int argc, char* argv[])
{
    using namespace std;
    using namespace chrono;
    using namespace tai;

    processArgs(argc, argv);
    srand(time(nullptr));

    vector<thread> threads;
    auto epoch = high_resolution_clock::now();
    for (size_t i = 0; i < thread_num; ++i)
    {
        auto rw = RandomWrite::getInstance(testType);
        threads.emplace_back([rw(move(rw)), i](){ rw->run(i); });
    }
    for (auto& t : threads)
        t.join();

    auto time = duration_cast<nanoseconds>(high_resolution_clock::now() - epoch).count();

    if (testType == 4)
        aio_end();

    Log::log(testname[testType], " random ", wlname[workload], ": ",
            time / 1e9, " s in total, ",
            IO_ROUND * (int(workload == 2) + 1), " ops/thread, ",
            READ_SIZE >> 10, " KB/read, " ,
            WRITE_SIZE >> 10, " KB/write, ", 
            thread_num, " threads, ",
            1e9 * IO_ROUND * (int(workload == 2) + 1) * thread_num / time, " iops");

    if (testType == 4)
    {
        auto rt = Log::run_time.load() / 1e9;
        auto st = Log::steal_time.load() / 1e9;
        auto bt = Log::barrier_time.load() / 1e9;
        Log::log("Total run time: " , rt, " s, steal time: ", st, " s, barrier time: ", bt, " s");
    }

    return 0;
}
