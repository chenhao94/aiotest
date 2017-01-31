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

constexpr int WRITE_SIZE = 1 << 16;
constexpr int IO_ROUND = 1<<2;//1 << 8;
constexpr int IO_SUBROUND_SIZE =1<<0;// 1 << 4;
constexpr int IO_SUBROUND = IO_ROUND / IO_SUBROUND_SIZE;
int sleep_time; // milliseconds
int thread_num;

auto randgen()
{
    // the RAND_MAX on my machine seems 2147483647,
    // and the fseek can only reach 2GB
    return rand() % (RAND_MAX - WRITE_SIZE);
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
        unsigned long long count = 0;

        for (int i = 0; i < IO_SUBROUND_SIZE; ++i)
        {
            auto offset = randgen();
            auto begin = high_resolution_clock::now();
            writeop(offset, data.data());
            count += duration_cast<nanoseconds>(high_resolution_clock::now() - begin).count(); 
            this_thread::sleep_for(milliseconds(sleep_time));
        }
        return count;
    }

    auto run(int thread_id)
    {
        unsigned long long time = 0;
        for (int i = 0; i < IO_SUBROUND; ++i)
        {
            if (!i || i * 10 / IO_SUBROUND > (i - 1) * 10 / IO_SUBROUND)
                cout << "[Thread " << thread_id << "]"<<  "Progess " << (i * 100 / IO_SUBROUND) << "\% finished." << endl;
            auto file = fopen(("mnt/file" + to_string(thread_id)).data(), "w+");
            fd = fileno(file);
            tai::register_fd(fd);
            time += run_sub();
            fclose(file);
            //tai::deregister_fd(fd);
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

    static void startEntry(int thread_id, unsigned long long &ret)
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

    static void startEntry(int thread_id, unsigned long long &ret)
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

    static void startEntry(int thread_id, unsigned long long &ret)
    {
        TAIWrite tw;
        ret = tw.run(thread_id);
    }
};

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        cerr << "need argument for sleep time (ms) and thread number" << endl;
        exit(-1);
    }
    sleep_time = stoi(argv[1]);
    thread_num = stoi(argv[2]);
    cout << "sleep time: " << sleep_time << "ms" << endl;
    cout << "thread number: " << thread_num << endl;
    srand(time(NULL));

    vector<unsigned long long> rvs(thread_num);

    {
        vector<thread> bthreads;
        for (auto i = 0; i < thread_num; ++i)
            bthreads.emplace_back(thread(BlockingWrite::startEntry, i, ref(rvs[i])));
        for (auto &t : bthreads)
            t.join();
        auto btime = accumulate(rvs.begin(), rvs.end(), 0ULL, plus<unsigned long long>());
        cout << "blocking random write: " << btime << " ns in total, " << IO_ROUND << " ops/thread, "
            << thread_num << " threads, " << 1e9 * IO_ROUND * thread_num / btime << " iops" << endl;
    }
    
    {
        vector<thread> athreads;
        for (auto i = 0; i < thread_num; ++i)
            athreads.emplace_back(thread(AIOWrite::startEntry, i, ref(rvs[i])));
        for (auto &t : athreads)
            t.join();
        auto atime = accumulate(rvs.begin(), rvs.end(), 0ULL, plus<unsigned long long>());
        cout << "Asynchronous random write: " << atime << " ns in total, " << IO_ROUND << " ops/thread, "
            << thread_num << " threads, " << 1e9 * IO_ROUND * thread_num / atime << " iops" << endl;
    }

    {
        tai::aio_init();
        vector<thread> tthreads;
        for (auto i = 0; i < thread_num; ++i)
            tthreads.emplace_back(thread(TAIWrite::startEntry, i, ref(rvs[i])));
        for (auto &t : tthreads)
            t.join();
        auto ttime = accumulate(rvs.begin(), rvs.end(), 0ULL, plus<unsigned long long>());
        cout << "TAI random write: " << ttime << " ns in total, " << IO_ROUND << " ops/thread, "
            << thread_num << " threads, " << 1e9 * IO_ROUND * thread_num / ttime << " iops" << endl;
        tai::aio_end();
    }

    return 0;
}
