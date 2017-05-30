#include <cstdlib>
#include <ctime>
#include <cassert>
#include <iostream>
#include <iomanip>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <memory>

#include "iotest.hpp"

void run(RandomWrite *rw, int tid)
{
    using namespace std;
    using namespace tai;
    char *data, *buf;
    rw->tid = tid;
    data = new
    #ifndef __MACH__
        (align_val_t(4096))
    #endif
        char[WRITE_SIZE * 2];
    memset(data, 'a', sizeof(WRITE_SIZE * 2));
    buf = new
    #ifndef __MACH__
        (align_val_t(4096))
    #endif
        char[READ_SIZE * 2]; 
    rw->reset_cb();
    assert(READ_SIZE == WRITE_SIZE);
    for (size_t i = 0; i < IO_ROUND; ++i)
    {
        auto px = randgen(READ_SIZE);
        auto py = randgen(READ_SIZE);
        auto pz = randgen(READ_SIZE);
        rw->readop(px, buf);
        rw->readop(py, buf + READ_SIZE);
        rw->wait_back(2);
        for (auto j = 16; j--; rw->writeop(randgen(WRITE_SIZE), data));
        rw->writeop(px, data);
        //rw->syncop();
        rw->writeop(py, data);
        rw->syncop();
        auto dxy = (long long *)data;
        auto dz = (long long *)(data + WRITE_SIZE);
        for (size_t j = 0; j < WRITE_SIZE / 8; ++j)
            for (size_t k = 0; k < WRITE_SIZE / 8; k += WRITE_SIZE / 1024)
                dz[j] = (dz[j] >> 1) ^ (dz[j] << sizeof(dz[j]) * 8 - 1) ^ dxy[k];
        rw->writeop(pz, data + WRITE_SIZE);
        rw->syncop();
        if (!i || i * 10 / IO_ROUND > (i - 1) * 10 / IO_ROUND)
            Log::log("[Thread ", rw->tid, "]", "Progess ", i * 100 / IO_ROUND, "\% finished.");
    }
    rw->syncop();
    rw->cleanup();
    operator delete[](data
            #ifndef __MACH__
            , align_val_t(4096)
            #endif
            );
    operator delete[](buf
            #ifndef __MACH__
            , align_val_t(4096)
            #endif
            );
}

int main(int argc, char* argv[])
{
    using namespace std;
    using namespace chrono;
    using namespace tai;

    processArgs(argc, argv);
    srand(time(nullptr));

    vector<thread> threads;
    auto rw = RandomWrite::getInstance(testType, true).release();
    rw->openfile("tmp/file0");

    auto start = high_resolution_clock::now();
    for (size_t i = 0; i < thread_num; ++i)
        threads.emplace_back([&rw](int i){ run(rw, i); }, i);
    for (auto& t : threads)
        t.join();
    rw->closefile();
    auto time = duration_cast<nanoseconds>(high_resolution_clock::now() - start).count();

    if (testType == 5 || testType == 6)
    {
        if (testType == 5)
            aio_end();
        else
            TAIWrite::end();
    }
    delete rw;
    Log::log(testname[testType], " TX test: ",
            time / 1e9, " s in total, ",
            IO_ROUND, " tx/thread, ",
            READ_SIZE >> 10, " KB/IO, " ,
            thread_num, " threads, ",
            1e9 * IO_ROUND * thread_num / time, " iops");
    return 0;
}
