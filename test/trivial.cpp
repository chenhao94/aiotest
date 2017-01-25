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
#include <vector>

#include "tai.hpp"

int main(int argc, char* argv[])
{
    using namespace std;
    using namespace chrono;
    using namespace this_thread;

    using namespace tai;

    int bs[5] = {4, 6, 3, 9, 1};
    int hashSync = 0, hashTAI = 0;

    vector<unique_ptr<IOCtrl>> ios;

    decltype(high_resolution_clock::now()) start;

    BTree<32,12,9,9,2> bt("tmp/tai");
    Controller ctrl(1 << 28, 1 << 30);

    vector<string> ss;
    for (int i = 0; i++ < 2; ss.emplace_back(10, 0));
    ss[0][0] = ss[0][8] = 'a';
    ios.emplace_back(bt.write(ctrl, 0, 9, ss[0].data()));
    ios.emplace_back(bt.readsome(ctrl, 0, 1, ss[1].data()));
    ios.emplace_back(bt.sync(ctrl));

    for (size_t i = 0; i < ios.size(); ++i)
    {
        auto& io = *ios[i];
        if (io.wait() != IOCtrl::Done)
            Log::log(to_cstring(io()));
    }

    return 0;
}
