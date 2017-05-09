#include <atomic>
#include <string>
#include <memory>

#include "iotest.hpp"

size_t thread_num = 1;
size_t testType;
size_t workload;
size_t FILE_SIZE = 1 << 31;
size_t READ_SIZE = 1 << 12;
size_t WRITE_SIZE = 1 << 12;
size_t IO_ROUND = 1 << 10;
size_t SYNC_RATE = 1;
size_t WAIT_RATE = 1;

bool SINGLE_FILE = false;
thread_local ssize_t RandomWrite::tid = 0;

#ifdef __linux__
io_context_t LibAIOWrite::io_cxt;
#endif
std::mutex LibAIOWrite::cntMtx;
size_t LibAIOWrite::cnt = 0;

std::unique_ptr<tai::Controller> TAIWrite::ctrl;

std::unique_ptr<RandomWrite> RandomWrite::getInstance(int testType, bool concurrent)
{
    using namespace std;

    static atomic_flag init = ATOMIC_FLAG_INIT;
    auto first = !init.test_and_set();

    unique_ptr<RandomWrite> rw;
    switch (testType)
    {
    case 0:
    case 1:
        rw.reset(new BlockingWrite());
        rw->openflags |= -testType & (
                            #ifdef __linux__
                            O_DIRECT |
                            #endif
                            O_SYNC);
        break;
    case 2:
        rw.reset(new AIOWrite());
        break;
    case 3:
        #ifdef __linux__
        if (first)
            io_setup(1048576, &LibAIOWrite::io_cxt);
        #endif
        rw.reset(new LibAIOWrite());
        break;
    case 4:
        if (first)
            tai::aio_init();
        rw.reset(new TAIAIOWrite());
        break;
    case 5:
        if (concurrent)
            rw.reset(new FstreamWrite<true>());
        else
            rw.reset(new FstreamWrite<false>());
        break;
    case 6:
        rw.reset(new TAIWrite());
        break;
    default:
        cerr << "Illegal IO-type! Exit..." << endl;
        exit(-1);
    }

    return rw;
}

