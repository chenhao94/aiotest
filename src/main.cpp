#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <memory>
#include <chrono>

#include "tai.hpp"

int main()
{
    using namespace std;
    using namespace chrono;
    using namespace this_thread;

    using namespace tai;

    cerr << "Creating B-tree...\n" << flush;
    BTreeDefault bt("tmp");

    cerr << "Creating Controller...\n" << flush;
    Controller ctrl(1 << 28, 1 << 30);

    cerr << "Writing...\n" << flush;
    unique_ptr<IOCtrl> io(bt.write(ctrl, 0, 10, (char*)"1234567890"));

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
