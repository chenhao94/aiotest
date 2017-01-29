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
#include <fcntl.h>

#include "aio.hpp"

int main(int argc, char* argv[])
{
    using namespace std;
    using namespace chrono;
    using namespace this_thread;

    using namespace tai;

    int bs[5] = {4, 6, 3, 9, 1};
    int hashSync = 0, hashTAI = 0;

    aio_init();
    vector<aiocb> cbs;

    auto file = fopen("tmp/tai", "w+");
    auto fd = fileno(file);
    register_fd(fd);

    vector<string> ss;
    for (int i = 0; i++ < 2; ss.emplace_back(10, 0));
    ss[0][0] = ss[0][8] = 'a';
    
    cbs.emplace_back(aiocb(fd, 0, ss[0].data(), 9));
    if ([&](){for (; auto err = aio_write(&cbs[0]); cout << "Busy" << endl) if (errno == EAGAIN) this_thread::sleep_for(seconds(1)); else return err; return 0;}())
    {
        cout << "Error[" << errno << "/" << strerror(errno) << "] at aio_write!" << endl;
        exit(-1);
    }
    else
        cout << "aio_write issued." << endl;

    cbs.emplace_back(aiocb(fd, 0, ss[1].data(), 1));
    if ([&](){for (; auto err = aio_read(&cbs[1]); cout << "Busy" << endl) if (errno == EAGAIN) this_thread::sleep_for(seconds(1)); else return err; return 0;}())
    {
        cout << "Error[" << errno << "/" << strerror(errno) << "] at aio_read!" << endl;
        exit(-1);
    }
    else
        cout << "aio_read issued." << endl;

    cbs.emplace_back(aiocb(fd));
    if ([&](){for (; auto err = aio_fsync(O_SYNC, &cbs[2]); cout << "Busy" << endl) if (errno == EAGAIN) this_thread::sleep_for(seconds(1)); else return err; return 0;}())
    {
        cout << "Error[" << errno << "/" << strerror(errno) << "] at aio_fsync!" << endl;
        exit(-1);
    }
    else
        cout << "aio_fsync issued." << endl;

    for (auto &i : cbs)
        while (aio_error(&i) == EINPROGRESS);

    cout << "Return:" << endl;
    for (auto &i : cbs)
        if (aio_error(&i))
            cout << "    <" << i.aio_reqprio << "> aio_error: " << aio_error(&i) << "/" << strerror(aio_error(&i)) << endl;
        else
            cout << "    <" << i.aio_reqprio << "> aio_return: " + to_string(aio_return(&i)) << endl;

    deregister_fd(fileno(file));

    return 0;
}
