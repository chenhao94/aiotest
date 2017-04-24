#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <string>
#include <thread>
#include <memory>
#include <chrono>
#include <random>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <aio.h>

#include "tai.hpp"

int main(int argc, char* argv[])
{
    using namespace std;
    using namespace chrono;
    using namespace this_thread;

    using namespace tai;

    const auto size = (size_t)1 << 30;
    const auto bs = (size_t)1 << 20;//1 << 16;
    auto n = (size_t)1 << 15;

    if (argc > 2)
        n = atoll(argv[2]);

    default_random_engine rand;
    uniform_int_distribution<size_t> dist(0, size - bs);

    string data(bs, 0);
    for (auto& i : data)
        i += rand();

    if (argc <= 1 || atoll(argv[1]) & 1)
    {
        decltype(high_resolution_clock::now()) start;

        {
            Log::log("Creating file for sync I/O...");

            fstream file("tmp/sync", ios_base::in | ios_base::out | ios_base::binary);

            if (!file.is_open())
                file.open("tmp/sync", ios_base::in | ios_base::out | ios_base::trunc | ios_base::binary);

            Log::log("Writing...");

            start = high_resolution_clock::now();

            for (auto i = n; i--; file.seekp(dist(rand) % size).write(data.data(), data.size()))
            {
                if (n < 100 || i % (n / 100) == 0)
                    Log::log("\t", (n - i - 1) * 100 / n, "% Performance: ", (size_t)round((n - i - 1) * 1e9 / (duration_cast<nanoseconds>(high_resolution_clock::now() - start).count() + 1)), " iops.");
                for (size_t j = 0; j < data.size(); ++j)
                    data[j] = n - i >> ((j & 7) << 3) & 255;
            }
        }

        Log::log("Performance: ", (size_t)round(n * 1e9 / (duration_cast<nanoseconds>(high_resolution_clock::now() - start).count() + 1)), " iops.");
    }

    if (argc <= 1 || atoll(argv[1]) & 2)
    {
        vector<aiocb> cbs;
        cbs.reserve(n + 1);

        decltype(high_resolution_clock::now()) start;

        {
            auto file = fopen("tmp/aio", "r+");
            auto fd = fileno(file);

            {
                start = high_resolution_clock::now();

                string* ss[1 << 15];

                for (auto i = n; i--;)
                {
                    for (size_t j = 0; j < data.size(); ++j)
                        data[j] = n - i >> ((j & 7) << 3) & 255;
                    cbs.emplace_back(aiocb());
                    cbs[n - i - 1].aio_fildes = fd;
                    cbs[n - i - 1].aio_buf = (ss[i] = new string(data))->data();
                    cbs[n - i - 1].aio_nbytes = data.size();
                    cbs[n - i - 1].aio_offset = dist(rand) % size;
                    aio_write(&cbs[n - i - 1]);
                }
                //cbs.emplace_back(aiocb());
                //cbs[n].aio_fildes = fd;
                //aio_fsync(O_SYNC, &cbs[n]);

                for (size_t i = 0; i < cbs.size(); ++i)
                {
                    auto& cb = cbs[i];
                    while (aio_error(&cb) == EINPROGRESS);
                    if (aio_error(&cb) && aio_error(&cb) != EINPROGRESS)
                    {
                        cerr << aio_error(&cb) << " Error " << errno << ": " << strerror(errno) << " at aio_error." << endl;
                        cerr << "reqprio: " << cb.aio_reqprio << ", offset: " << cb.aio_offset << " , nbytes: " << cb.aio_nbytes << endl; 
                        exit(-1);
                    }
                    if (cbs.size() < 100 || i % (cbs.size() / 100) == 0)
                        Log::log("\t", i * 100 / cbs.size(), "% Performance: ", (size_t)round(i * 1e9 / (duration_cast<nanoseconds>(high_resolution_clock::now() - start).count() + 1)), " iops.");
                }

                for (auto i = n; i--; delete ss[i]);
            }
        }

        Log::log("Performance: ", (size_t)round(n * 1e9 / (duration_cast<nanoseconds>(high_resolution_clock::now() - start).count() + 1)), " iops.");
    }

    if (argc <= 1 || atoll(argv[1]) & 4)
    {
        vector<unique_ptr<IOCtrl>> ios;
        ios.reserve(n + 1);

        decltype(high_resolution_clock::now()) start;

        {
            Log::log("Creating B-tree...");
            BTreeDefault bt(new POSIXEngine("tmp/tai"));

            {
                Log::log("Creating Controller...");
                Controller ctrl(1 << 28, 1 << 30);

                Log::log("Writing...");

                start = high_resolution_clock::now();

                string* ss[1 << 15];

                for (auto i = n; i--; ios.emplace_back(bt.write(ctrl, dist(rand) % size, data.size(), (ss[i]=(new string(data)))->data())))
                    for (size_t j = 0; j < data.size(); ++j)
                        data[j] = n - i >> ((j & 7) << 3) & 255;
                ios.emplace_back(bt.sync(ctrl));

                for (size_t i = 0; i < ios.size(); ++i)
                {
                    auto& io = *ios[i];
                    if (io.wait() != IOCtrl::Done)
                        Log::log(to_cstring(io()));
                    if (ios.size() < 100 || i % (ios.size() / 100) == 0)
                        Log::log("\t", i * 100 / ios.size(), "% Performance: ", (size_t)round(i * 1e9 / (duration_cast<nanoseconds>(high_resolution_clock::now() - start).count() + 1)), " iops.");
                }

                for (auto i = n ; i--; delete ss[i]);
            }
        }

        Log::log("Performance: ", (size_t)round(n * 1e9 / (duration_cast<nanoseconds>(high_resolution_clock::now() - start).count() + 1)), " iops.");
    }

    Log::log("Complete!");
    return 0;
}
