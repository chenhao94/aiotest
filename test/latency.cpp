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
#include <algorithm>

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

int main(int argc, char *argv[])
{
    if (argc != 10)
    {
        cerr << "[Same CL options as MT]Need arguments for type of IO to test, write size and number of IOs issuing once." << endl;
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

    READ_SIZE = READ_SIZE << 10;
    WRITE_SIZE = WRITE_SIZE << 10;

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

    auto sum_issue = 0ull, sum_sync = 0ull; 
    auto tot_rnd = IO_ROUND / SYNC_RATE;
    auto pcnt20 = tot_rnd / 5, pcnt80 = tot_rnd - pcnt20;
    vector<double> issue;
    vector<double> sync;
    for (int T = 0; T < tot_rnd; ++T)
    {
        rw->reset_cb();

        auto start = high_resolution_clock::now();
        for (int i = SYNC_RATE; i--; )
        {
            auto offset = randgen(WRITE_SIZE);
            rw->writeop(offset, data);
        }
        auto mid = high_resolution_clock::now();
        rw->syncop();
        rw->busywait_cb();
        auto end = high_resolution_clock::now();
        issue.push_back(1e-3 * duration_cast<nanoseconds>(mid - start).count());
        sync.push_back(1e-3 * duration_cast<nanoseconds>(end - mid).count());
    }
    sort(issue.begin(), issue.end());
    sort(sync.begin(), sync.end());
    auto avg_st_issue = issue.begin() + pcnt20, avg_en_issue = issue.begin() + pcnt80 + 1;
    double mid_issue = issue[issue.size() / 2], pcnt20_issue = issue[pcnt20], pcnt80_issue = issue[pcnt80];
    double avg_issue = accumulate(avg_st_issue,  avg_en_issue, 0.) / (avg_en_issue - avg_st_issue);

    auto avg_st_sync = sync.begin() + pcnt20, avg_en_sync = sync.begin() + pcnt80 + 1;
    double mid_sync = sync[sync.size() / 2], pcnt20_sync = sync[pcnt20], pcnt80_sync = sync[pcnt80];
    double avg_sync = accumulate(avg_st_sync,  avg_en_sync, 0.) / (avg_en_sync - avg_st_sync);

    rw->cleanup();
    tai::deregister_fd(rw->fd);
    if (testType == 4)
        tai::aio_end();

    string testname[] = {"BIO", "DIO", "PAIO", "LAIO", "TAI"};
    tai::Log::log( testname[testType], " ", 
            "testType, X of IO, size per IO(KB), 20 percentile of issuing(us), average, median, 80 percentile, ",
            "20 percentile of syncing, average, median, 80 percentile");
    cout << testname[testType] << ", " << SYNC_RATE << ", " << (WRITE_SIZE >> 10) << ", " << pcnt20_issue << ", " << avg_issue << ", " << mid_issue << ", " <<pcnt80_issue;
    cout << ", " << pcnt20_sync << ", " << avg_sync << ", " << mid_sync << ", " <<pcnt80_sync << endl;
    close(rw->fd);
    return 0;
}
