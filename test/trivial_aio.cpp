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
    if (!register_fd(fd, "tmp/tai"))
    {
        cerr << "Register fd Error!" << endl;
        exit(-1);
    }

    vector<string> ss;
    for (int i = 0; i++ < 2; ss.emplace_back(10, 0));
    ss[0][0] = ss[0][8] = 'a';
    
    cbs.emplace_back(aiocb(fd, 0, ss[0].data(), 9));
    if ([&](){for (; auto err = aio_write(&cbs[0]); Log::log("Busy")) if (errno == EAGAIN) this_thread::sleep_for(seconds(1)); else return err; return 0;}())
    {
        Log::log("Error[", errno, "/", strerror(errno), "] at aio_write!");
        exit(-1);
    }
    else
        Log::log("aio_write issued.");

    cbs.emplace_back(aiocb(fd, 0, ss[1].data(), 1));
    if ([&](){for (; auto err = aio_read(&cbs[1]); Log::log("Busy")) if (errno == EAGAIN) this_thread::sleep_for(seconds(1)); else return err; return 0;}())
    {
        Log::log("Error[", errno, "/", strerror(errno), "] at aio_read!");
        exit(-1);
    }
    else
        Log::log("aio_read issued.");

    cbs.emplace_back(aiocb(fd));
    if ([&](){for (; auto err = aio_fsync(O_SYNC, &cbs[2]); cout << "Busy" << endl) if (errno == EAGAIN) this_thread::sleep_for(seconds(1)); else return err; return 0;}())
    {
        Log::log("Error[", errno, "/", strerror(errno), "] at aio_fsync!");
        exit(-1);
    }
    else
        Log::log("aio_fsync issued.");

    for (auto &i : cbs)
        while (aio_error(&i) == EINPROGRESS);

    Log::log("Return:");
    for (auto &i : cbs)
        if (aio_error(&i))
            Log::log("    <", i.aio_reqprio, "> aio_error: ", aio_error(&i), "/", strerror(aio_error(&i)));
        else
            Log::log("    <", i.aio_reqprio, "> aio_return: ", aio_return(&i));

    deregister_fd(fileno(file));
    aio_end();

    return 0;
}
