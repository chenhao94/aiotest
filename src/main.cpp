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

    cerr << "Creating B-tree...\n" << flush;
    BTreeDefault bt("tmp");

    cerr << "Creating Controller...\n" << flush;
    Controller ctrl(1 << 28, 1 << 30);

    cerr << "Writing...\n" << flush;

    string data(10000, '0');
    for (auto& i : data)
        i += rand() % 10;

    for (auto i = 1; i--; bt.write(ctrl, 0, data.size(), data.data()));

    unique_ptr<IOCtrl> io(bt.sync(ctrl));

    for (; (*io)() == IOCtrl::Running; sleep_for(seconds(1)))
        cerr << "Still running...\n" << flush;

    switch ((*io)())
    {
    case IOCtrl::Running:
        cerr << "\tRunning\n";
        break;
    case IOCtrl::Failed:
        cerr << "\tFailed\n";
        break;
    case IOCtrl::Rejected:
        cerr << "\tRejected\n";
        break;
    case IOCtrl::Done:
        cerr << "\tDone\n";
        break;
    default:
        cerr << "\tUnknown IOCtrl state\n";
    }
    cerr << flush;

    cerr << "Complete!\n" << flush;
    return 0;
}
