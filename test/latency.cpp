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
#include "./lib/iotest.hpp"

#define unlikely(x)     __builtin_expect((x),0)

using namespace std;
using namespace chrono;
using namespace this_thread;

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        cerr << "Need arguments for type of IO to test, write size and number of IOs issuing once." << endl;
        exit(-1);
    }
    [&](vector<size_t*> _){ for (auto i = _.size(); i--; *_[i] = stoll(argv[i + 1])); }({
            &testType,
            &WRITE_SIZE,
            &IO_ROUND
            });
    READ_SIZE = WRITE_SIZE = WRITE_SIZE << 10;

    RandomWrite *rw;

    switch (testType)
    {
    case 0:
    case 1:
        rw = new BlockingWrite();
        rw->openflags |= -testType & (
                            #ifdef __linux__
                            O_DIRECT |
                            #endif
                            O_SYNC);
        break;
    case 2:
        rw = new AIOWrite();
        break;
    case 3:
        #ifdef __linux__
        io_setup(1048576, &LibAIOWrite::io_cxt);
        #endif
        rw = new LibAIOWrite();
        break;
    case 4:
        tai::aio_init();
        rw = new TAIWrite();
        break;
    default:
        cerr << "Illegal IO-type! Exit..." << endl;
        return 0;
    }

    rw->fd = open("tmp/file", rw->openflags); 
    tai::register_fd(rw->fd, "tmp/file");

    auto data = new(align_val_t(512)) char[WRITE_SIZE];
    memset(data, 'a', sizeof(WRITE_SIZE));

    auto TEST_ROUND = 1 << 10;
    auto sum_issue = 0ull, sum_sync = 0ull; 
    for (int T = 0; T < 1024; ++T)
    {
        rw->reset_cb();

        auto start = high_resolution_clock::now();
        for (int i = IO_ROUND; i--; )
        {
            auto offset = randgen(WRITE_SIZE);
            rw->writeop(offset, data);
        }
        auto mid = high_resolution_clock::now();
        rw->syncop();
        rw->busywait_cb();
        auto end = high_resolution_clock::now();
        sum_issue += std::chrono::duration_cast<std::chrono::nanoseconds>(mid - start).count();
        sum_sync += std::chrono::duration_cast<std::chrono::nanoseconds>(end - mid).count();
    }

    rw->cleanup();
    tai::deregister_fd(rw->fd);
    if (testType == 4)
        tai::aio_end();

    string testname[] = {"Blocking IO", "Blocking IO (O_DIRECT | O_SYNC)", "Async IO", "LibAIO", "TAI"};
    tai::Log::log( testname[testType], " ", 
            IO_ROUND, "x " , WRITE_SIZE >> 10, "KB writes; ", 
            "Avg total issue time: ", sum_issue * 1e-3 / TEST_ROUND, " us ; avg total sync time: ", sum_sync * 1e-3 / TEST_ROUND, " us");
    close(rw->fd);
    return 0;
}
