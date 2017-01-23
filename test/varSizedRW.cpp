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

#include "tai.hpp"

static const auto mod = 2038074161;

inline int hashAdd(int x, int y)
{
    return ( 256ll * x + y ) % mod;
}

int main(int argc, char* argv[])
{
    using namespace std;
    using namespace chrono;
    using namespace this_thread;

    using namespace tai;

    const auto size = (size_t)100;//1 << 30;
    const auto maxbs = (size_t)5;//1 << 16;
    auto n = (size_t)1 << 7;

    if (argc > 1)
        n = atoll(argv[1]);

    default_random_engine rand;
    uniform_int_distribution<size_t> dist(0, size - maxbs);

    string data(maxbs, 0);
    //for (auto& i : data)
    //    i += rand();

    int hashSync = 0, hashTAI = 0;

    // sync IO
    {
        rand.seed(1234321);
        decltype(high_resolution_clock::now()) start;

        {
            Log::log("Creating file for sync I/O...");

            fstream file("tmp/sync", ios_base::in | ios_base::out | ios_base::binary);
            int hashV = 0;

            if (!file.is_open())
                file.open("tmp/sync", ios_base::in | ios_base::out | ios_base::trunc | ios_base::binary);

            Log::log("Processing...");

            start = high_resolution_clock::now();

            for (auto i = n; i--;)
            {
                for (size_t j = 0; j < data.size(); ++j)
                    data[j] = n - i >> ((j & 7) << 3) & 255;
                auto bs = rand() % (maxbs + 1);
                if (rand() & 1)
                    file.seekp(/*dist(rand) % size*/0).write(data.data(), bs), cout << "write" << endl;
                else
                {
                    file.seekg(/*dist(rand) % size*/0).read(data.data(), bs), cout << "read" << endl;
                    file.clear();
                    for (;bs--;)
                        hashSync = hashAdd(hashSync, data[bs]);
                }
                cout << "hashSync: " << hashSync << endl;
                if (n < 100 || i % (n / 100) == 0)
                    Log::log("\t", (n - i - 1) * 100 / n, "% Performance: ", (size_t)round((n - i - 1) * 1e9 / (duration_cast<nanoseconds>(high_resolution_clock::now() - start).count() + 1)), " iops.");
            }
        }

        Log::log("Performance: ", (size_t)round(n * 1e9 / (duration_cast<nanoseconds>(high_resolution_clock::now() - start).count() + 1)), " iops.");
    }

    // TAI
    {
        rand.seed(1234321);
        vector<unique_ptr<IOCtrl>> ws;
        vector<unique_ptr<IOCtrl>> rs;
        vector<int> bss;
        vector<int> pos;
        ws.reserve(n + 1);
        rs.reserve(n + 1);
        bss.reserve(n + 1);
        pos.reserve(n + 1);

        decltype(high_resolution_clock::now()) start;

        {
            Log::log("Creating B-tree...");
            BTreeDefault bt("tmp/tai");
            // BTreeTrivial bt("tmp/tai");

            {
                Log::log("Creating Controller...");
                // Controller ctrl(1 << 28, 1 << 30, 1);
                Controller ctrl(1 << 28, 1 << 30);

                Log::log("Processing...");

                start = high_resolution_clock::now();

                string* ss[1<<15];

                for (auto i = n; i--; )
                {
                    for (size_t j = 0; j < data.size(); ++j)
                        data[j] = n - i >> ((j & 7) << 3) & 255;
                    auto bs = rand() % (maxbs + 1);
                    if (rand() & 1)
                        ws.emplace_back(bt.write(ctrl, /*dist(rand) % size*/0, bs, (ss[i]=(new string(data)))->data()));
                    else
                    {
                        ss[i]=new string(maxbs, 0);
                        cout << "TAI read: " << bs << endl;
                        rs.emplace_back(bt.readsome(ctrl, /*dist(rand) % size*/0, bs, ss[i]->data()));
                        bss.push_back(bs);
                        pos.push_back(i);
                    }
                }
                rs.emplace_back(bt.sync(ctrl));
                bss.push_back(0);
                pos.push_back(0);

                for (size_t i = 0; i < ws.size(); ++i)
                {
                    auto& io = *ws[i];
                    if (io.wait() != IOCtrl::Done)
                        Log::log(to_cstring(io()));
                    if (ws.size() < 100 || i % (ws.size() / 100) == 0)
                        Log::log("\t", i * 100 / ws.size(), "% Performance: ", (size_t)round(i * 1e9 / (duration_cast<nanoseconds>(high_resolution_clock::now() - start).count() + 1)), " iops.");
                }

                for (size_t i = 0; i < rs.size(); ++i)
                {
                    auto& io = *rs[i];
                    auto bs = bss[i];
                    auto p = pos[i];
                    if (io.wait() != IOCtrl::Done)
                        Log::log(to_cstring(io()));
                    for (;bs--;)
                        hashTAI = hashAdd(hashTAI, (int)ss[p]->at(bs));
                    cout << "hashTAI: " << hashTAI << endl;
                    if (rs.size() < 100 || i % (rs.size() / 100) == 0)
                        Log::log("\t", i * 100 / rs.size(), "% Performance: ", (size_t)round(i * 1e9 / (duration_cast<nanoseconds>(high_resolution_clock::now() - start).count() + 1)), " iops.");
                }

                for (auto i = n ; i--; delete ss[i]);
            }
        }

        Log::log("Performance: ", (size_t)round(n * 1e9 / (duration_cast<nanoseconds>(high_resolution_clock::now() - start).count() + 1)), " iops.");
    }

    Log::log("hashSync: ", hashSync, "; hashTAI: ", hashTAI);
    Log::log((hashSync == hashTAI)?"Passed!":"Failed!");
    return 0;
}
