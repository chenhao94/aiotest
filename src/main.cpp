#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <memory>
#include <chrono>
#include <random>

#include "tai.hpp"

int main()
{
    using namespace std;
    using namespace chrono;
    using namespace this_thread;

    using namespace tai;

    default_random_engine rand;

    Log::log("Creating B-tree...");
    BTreeDefault bt("tmp");

    Log::log("Creating Controller...");
    Controller ctrl(1 << 28, 1 << 30);

    Log::log("Writing...");

    string data(10000, '0');
    for (auto& i : data)
        i += rand() % 10;

    // for (auto i = 1; i--; bt.write(ctrl, 0, data.size(), data.data()));

    // unique_ptr<IOCtrl> io(bt.sync(ctrl));
    unique_ptr<IOCtrl> io(bt.write(ctrl, 0, data.size(), data.data()));

    for (; (*io)() == IOCtrl::Running; sleep_for(seconds(1)))
        Log::log("Still running...");

    Log::log(to_cstring((*io)()));

    Log::log("Complete!");
    return 0;
}
