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

    time_point<high_resolution_clock> epoch;
    long long time;
    if (SINGLE_FILE)
    {
        auto rw = RandomWrite::getInstance(testType, true).release();
        epoch = high_resolution_clock::now();
        for (size_t i = 0; i < thread_num; ++i)
            threads.emplace_back([=](){ rw->run(i); });
        for (auto& t : threads)
            t.join();
        time = duration_cast<nanoseconds>(high_resolution_clock::now() - epoch).count();
        delete rw;
    }
    else
    {
        vector<unique_ptr<RandomWrite>> rw;
        for (size_t i = 0; i < thread_num; ++i)
            rw.emplace_back(RandomWrite::getInstance(testType));
        epoch = high_resolution_clock::now();
        for (size_t i = 0; i < rw.size(); ++i)
            threads.emplace_back([=](auto&& rw){ rw->run(i); }, move(rw[i]));
        for (auto& t : threads)
            t.join();
        time = duration_cast<nanoseconds>(high_resolution_clock::now() - epoch).count();
    }

    Log::log(testname[testType], " random ", wlname[workload], ": ",
            time / 1e9, " s in total, ",
            IO_ROUND * (int(workload == 2) + 1), " ops/thread, ",
            READ_SIZE >> 10, " KB/read, " ,
            WRITE_SIZE >> 10, " KB/write, ", 
            thread_num, " threads, ",
            1e9 * IO_ROUND * (int(workload == 2) + 1) * thread_num / time, " iops");

    if (testType == 4 || testType == 6)
    {
        if (testType == 4)
            aio_end();
        else
            TAIWrite::end();

        auto rt = Log::run_time.load() / 1e9;
        auto st = Log::steal_time.load() / 1e9;
        auto bt = Log::barrier_time.load() / 1e9;
        Log::log("Total run time: " , rt, " s, steal time: ", st, " s, barrier time: ", bt, " s");
    }

    return 0;
}
