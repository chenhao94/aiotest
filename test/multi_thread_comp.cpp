#include <cstdlib>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <memory>

#include "iotest.hpp"

template<bool read, bool write>
static void run_common(RandomWrite* rw)
{
    using namespace std;
    using namespace tai;

    char* data = nullptr;
    char* buf = nullptr;
    rw->openfile("tmp/file" + to_string(SINGLE_FILE ? 0 : rw->tid));
    if (write)    
    {
        data = new
            #ifndef __MACH__
            //(align_val_t(4096))
            #endif
            char[WRITE_SIZE];
        memset(data, 'a', sizeof(WRITE_SIZE));
    }
    if (read)
    {
       buf = new
            #ifndef __MACH__
            //(align_val_t(4096))
            #endif
            char[READ_SIZE * WAIT_RATE]; 
    }
    vector<size_t> offs;
    offs.reserve(IO_ROUND);
    rw->reset_cb();
    for (size_t i = 0; i < IO_ROUND; ++i)
    {
        if (write)
        {
            if (i && !(i & ~-SYNC_RATE))
            {
                rw->syncop();
                if (!(i & ~-WAIT_RATE))
                {
                    if (read)
                        for (auto j = i - WAIT_RATE; j < i; ++j)
                            rw->readop(offs[j], buf + (j & ~-WAIT_RATE) * READ_SIZE);
                    rw->wait_cb();
                }
            }
            offs.emplace_back(randgen(WRITE_SIZE));
            rw->writeop(offs.back(), data);
        }
        else if (read)  // Read-only
        {
            if (i && !(i & ~-WAIT_RATE))
                rw->wait_cb();
            rw->readop(randgen(READ_SIZE), buf + (i & ~-WAIT_RATE) * READ_SIZE);
        }
        if (!i || i * 10 / IO_ROUND > (i - 1) * 10 / IO_ROUND)
            Log::log("[Thread ", rw->tid, "]", "Progess ", i * 100 / IO_ROUND, "\% finished.");
    }
    if (read && write)
        for (auto j = IO_ROUND - WAIT_RATE; j < IO_ROUND; ++j)
            rw->readop(offs[j], buf + (j & ~-WAIT_RATE) * READ_SIZE);
    rw->closefile();
    operator delete[](data
            #ifndef __MACH__
            //, align_val_t(4096)
            #endif
            );
    operator delete[](buf
            #ifndef __MACH__
            //, align_val_t(4096)
            #endif
            );
}

void run(RandomWrite* rw, int tid)
{
    using namespace std;

    rw->tid = tid;

    array<function<void()>, 3>{
        [=](){ run_common<1, 0>(rw); },
        [=](){ run_common<0, 1>(rw); },
        [=](){ run_common<1, 1>(rw); }
    }[workload]();
}

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
            threads.emplace_back([&rw, i](){ run(rw, i); });
        for (auto& t : threads)
            t.join();
        time = duration_cast<nanoseconds>(high_resolution_clock::now() - epoch).count();
        delete rw;
    }
    else
    {
        vector<RandomWrite*> rw;
        for (size_t i = 0; i < thread_num; ++i)
            rw.emplace_back(RandomWrite::getInstance(testType).release());
        epoch = high_resolution_clock::now();
        for (size_t i = 0; i < rw.size(); ++i)
            threads.emplace_back([&rw, i](){ run(rw[i], i); });
        for (auto& t : threads)
            t.join();
        time = duration_cast<nanoseconds>(high_resolution_clock::now() - epoch).count();
        for (auto i : rw)
            delete i;
    }

    Log::log(testname[testType], " random ", wlname[workload], ": ",
            time / 1e9, " s in total, ",
            #ifdef _POSIX_VERSION
            __aio_lockedtime() / 1e9, " s holding lock, ",
            #endif
            IO_ROUND * (int(workload == 2) + 1), " ops/thread, ",
            READ_SIZE >> 10, " KB/read, " ,
            WRITE_SIZE >> 10, " KB/write, ", 
            thread_num, " threads, ",
            1e9 * IO_ROUND * (int(workload == 2) + 1) * thread_num / time, " iops");

//    if (testType == 5 || testType == 6)
//    {
//        if (testType == 5)
//            aio_end();
//        else
//            TAIWrite::end();
//
//        auto rt = Log::run_time.load() / 1e9;
//        auto st = Log::steal_time.load() / 1e9;
//        auto bt = Log::barrier_time.load() / 1e9;
//        Log::log("Total run time: " , rt, " s, steal time: ", st, " s, barrier time: ", bt, " s");
//    }

    return 0;
}
