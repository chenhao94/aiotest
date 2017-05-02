#include <vector>
#include <string>
#include <memory>

#include "iotest.hpp"

std::string testname[] = {"Posix", "DIO", "PosixAIO", "LibAIO", "TaiAIO", "STL", "Tai"};
std::string wlname[] = {"read", "write", "read&write"};

void processArgs(int argc, char* argv[])
{
    using namespace std;

    if (argc < 10)
    {
        cerr << "Need arguments for thread number, type of IO to test and workload type" << endl;
        exit(-1);
    }
    [&](vector<size_t*> _){ for (auto i = _.size(); i--; *_[i] = stoll(argv[i + 1])); }({
            &thread_num,
            &testType,
            &workload,
            });
    [&](vector<size_t*> _){ for (auto i = _.size(); i--; *_[i] = 1ll << stoll(argv[i + 4])); }({
            &FILE_SIZE,
            });
    [&](vector<size_t*> _){ for (auto i = _.size(); i--; *_[i] = stoll(argv[i + 5])); }({
            &READ_SIZE,
            &WRITE_SIZE,
            });
    [&](vector<size_t*> _){ for (auto i = _.size(); i--; *_[i] = 1ll << stoll(argv[i + 7])); }({
            &IO_ROUND,
            &SYNC_RATE,
            &WAIT_RATE
            });
    if (argc > 10)
        SINGLE_FILE = (stoll(argv[10])>0);

    READ_SIZE = READ_SIZE << 10;
    WRITE_SIZE = WRITE_SIZE << 10;

    tai::Log::log("thread number: ", thread_num);
    tai::Log::log(testname[testType], SINGLE_FILE?" on single file":" on multiple files");
}

std::unique_ptr<RandomWrite> RandomWrite::getInstance(int testType, bool concurrent)
{
    static std::atomic_flag init = ATOMIC_FLAG_INIT;
    auto first = !init.test_and_set();

    std::unique_ptr<RandomWrite> rw;
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
        std::cerr << "Illegal IO-type! Exit..." << std::endl;
        exit(-1);
    }

    return rw;
}

