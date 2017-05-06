#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <thread>
#include <new>
#include <memory>

#include "iotest.hpp"

int main(int argc, char *argv[])
{
    using namespace std;
    using namespace chrono;

    using namespace tai;

    processArgs(argc, argv);

    auto rw = RandomWrite::getInstance(testType);
    rw->openfile("tmp/file0");

    unique_ptr<char> data(new
        #ifdef __linux__
        (align_val_t(512))
        #endif
        char[WRITE_SIZE]);
    memset(data.get(), 'a', WRITE_SIZE);

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
            rw->writeop(offset, data.get());
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

    Log::log("testType, X of IO, size per IO(KB),",
            " 20 percentile of issuing(us), average, median, 80 percentile,",
            " 20 percentile of syncing, average, median, 80 percentile");
    cout << testname[testType] << ", " << SYNC_RATE << ", " << (WRITE_SIZE >> 10) << ", " << pcnt20_issue << ", " << avg_issue << ", " << mid_issue << ", " <<pcnt80_issue;
    cout << ", " << pcnt20_sync << ", " << avg_sync << ", " << mid_sync << ", " <<pcnt80_sync << endl;

    rw->cleanup();
    rw->closefile();

    if (testType == 4)
        aio_end();
    if (testType == 6)
        TAIWrite::end();

    return 0;
}
