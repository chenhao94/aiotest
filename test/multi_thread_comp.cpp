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

#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <aio.h>

#include "tai.hpp"
#include "aio.hpp"

using namespace std;
using namespace chrono;
using namespace this_thread;

constexpr size_t    WRITE_SIZE = 1 << 16;
constexpr size_t    FILE_SIZE = 1 << 30;
constexpr size_t    IO_ROUND = 1 << 8;
constexpr size_t    IO_SUBROUND_SIZE = 1 << 4;
constexpr auto      IO_SUBROUND = IO_ROUND / IO_SUBROUND_SIZE;

size_t sleep_time; // milliseconds
size_t thread_num;

auto randgen()
{
    return rand() % (FILE_SIZE - WRITE_SIZE);
}

class RandomWrite;
class AIOWrite;
class BlockingWrite;
class TAIWrite;

class RandomWrite
{
public:
    int fd;

    RandomWrite() 
    {
        if (RAND_MAX < numeric_limits<int>::max())
        {
            cerr << "RAND_MAX smaller than expected." << endl;
            exit(-1);
        }
    }

    auto run_sub()
    {
        vector<char> data(WRITE_SIZE, 'a');
        auto count = 0ull;

        for (size_t i = 0; i < IO_SUBROUND_SIZE; ++i)
        {
            auto offset = randgen();
            auto begin = high_resolution_clock::now();
            writeop(offset, data.data());
            count += duration_cast<nanoseconds>(high_resolution_clock::now() - begin).count(); 
            if (sleep_time > 0)
                sleep_for(milliseconds(sleep_time));
        }
        return count;
    }

    auto run(size_t thread_id)
    {
        auto time = 0ull;
        for (size_t i = 0; i < IO_SUBROUND; ++i)
        {
            if (!i || i * 30 / IO_SUBROUND > (i - 1) * 30 / IO_SUBROUND)
                tai::Log::log("[Thread ", thread_id, "]", "Progess ", i * 100 / IO_SUBROUND, "\% finished.");
            auto file = fopen(("tmp/file" + to_string(thread_id)).data(), "w+");
            fd = fileno(file);
            tai::register_fd(fd, "tmp/file" + to_string(thread_id));
            time += run_sub();
            sleep_for(1s);
            tai::deregister_fd(fd);
            fclose(file);
        }
        return time;
    }

    virtual void writeop(off_t offset, char* data) = 0;
};

class BlockingWrite : public RandomWrite
{
public: 

    BlockingWrite() : RandomWrite() {}

    void writeop(off_t offset, char* data) override
    {
        if (lseek(fd, offset, SEEK_SET) < 0)
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at lseek." << endl;
            exit(-1);
        }
        if (write(fd, data, WRITE_SIZE) < 0)
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at write." << endl;
            exit(-1);
        }
    }

    static void startEntry(size_t thread_id, unsigned long long &ret)
    {
        BlockingWrite bw;
        ret = bw.run(thread_id);
    }
};

class AIOWrite : public RandomWrite
{
public:

    AIOWrite() : RandomWrite() {}

    void writeop(off_t offset, char* data) override
    {
        aiocb cb;
        memset(&cb, 0, sizeof(aiocb));
        cb.aio_fildes = fd;
        cb.aio_nbytes = WRITE_SIZE;
        cb.aio_buf = data;
        cb.aio_offset = offset;
        if (aio_write(&cb))
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at aio_write." << endl;
            exit(-1);
        }
        while (aio_error(&cb));
        if (aio_error(&cb) && aio_error(&cb) != EINPROGRESS)
        {
            cerr << aio_error(&cb) <<  " Error " << errno << ": " << strerror(errno) << " at aio_error." << endl;
            cerr << "reqprio: " << cb.aio_reqprio << ", offset: " << cb.aio_offset << " , nbytes: " << cb.aio_nbytes << endl; 
            exit(-1);
        }
        aio_return(&cb);
    }

    static void startEntry(size_t thread_id, unsigned long long &ret)
    {
        AIOWrite aw;
        ret = aw.run(thread_id);
    }
};

class TAIWrite : public RandomWrite
{
public: 

    TAIWrite() : RandomWrite() {}

    void writeop(off_t offset, char* data)
    {
        tai::aiocb cb(fd, offset, data, WRITE_SIZE);
        if (tai::aio_write(&cb))
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at aio_write." << endl;
            exit(-1);
        }
        while (tai::aio_error(&cb));
        if (tai::aio_error(&cb) && tai::aio_error(&cb) != EINPROGRESS)
        {
            cerr << tai::aio_error(&cb) <<  " Error " << errno << ": " << strerror(errno) << " at aio_error." << endl;
            cerr << "reqprio: " << cb.aio_reqprio << ", offset: " << cb.aio_offset << " , nbytes: " << cb.aio_nbytes << endl; 
            exit(-1);
        }
        tai::aio_return(&cb);
    }

    static void startEntry(size_t thread_id, unsigned long long &ret)
    {
        TAIWrite tw;
        ret = tw.run(thread_id);
    }
};

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        cerr << "need argument for sleep time (ms) and thread number" << endl;
        exit(-1);
    }
    sleep_time = stoll(argv[1]);
    thread_num = stoll(argv[2]);
    tai::Log::log("sleep time: ", sleep_time, "ms");
    tai::Log::log("thread number: ", thread_num);
    srand(time(nullptr));

    vector<unsigned long long> rvs(thread_num);

    if (argc < 4 || stoll(argv[3]) & 1)
    {
        system("sync");
        vector<thread> bthreads;
        for (size_t i = 0; i < thread_num; ++i)
            bthreads.emplace_back(thread(BlockingWrite::startEntry, i, ref(rvs[i])));
        for (auto &t : bthreads)
            t.join();
        auto btime = accumulate(rvs.begin(), rvs.end(), 0ull);
        tai::Log::log("Blocking random write: ", btime, " ns in total, ", IO_ROUND, " ops/thread, ",
                thread_num, " threads, ", 1e9 * IO_ROUND * thread_num / btime, " iops");
    }

    if (argc < 4 || stoll(argv[3]) & 2)
    {
        system("sync");
        vector<thread> athreads;
        for (size_t i = 0; i < thread_num; ++i)
            athreads.emplace_back(thread(AIOWrite::startEntry, i, ref(rvs[i])));
        for (auto &t : athreads)
            t.join();
        auto atime = accumulate(rvs.begin(), rvs.end(), 0ull);
        tai::Log::log("Asynchronous random write: ", atime, " ns in total, ", IO_ROUND, " ops/thread, ",
                thread_num, " threads, ", 1e9 * IO_ROUND * thread_num / atime, " iops");
    }

    if (argc < 4 || stoll(argv[3]) & 4)
    {
        system("sync");
        tai::aio_init();
        vector<thread> tthreads;
        for (size_t i = 0; i < thread_num; ++i)
            tthreads.emplace_back(thread(TAIWrite::startEntry, i, ref(rvs[i])));
        for (auto &t : tthreads)
            t.join();
        auto ttime = accumulate(rvs.begin(), rvs.end(), 0ull);
        tai::Log::log("TAI random write: ", ttime, " ns in total, ", IO_ROUND, " ops/thread, ",
                thread_num, " threads, ", 1e9 * IO_ROUND * thread_num / ttime, " iops");
        tai::aio_end();
    }

    system("sync");

    return 0;
}
