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
#include <new>

#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <aio.h>

#ifdef __linux__
#include <libaio.h>
#endif

#include "tai.hpp"
#include "aio.hpp"

#define unlikely(x)     __builtin_expect((x),0)

using namespace std;
using namespace chrono;
using namespace this_thread;

size_t thread_num;
size_t testType;
size_t workload;
size_t FILE_SIZE;
size_t READ_SIZE;
size_t WRITE_SIZE;
size_t IO_ROUND;
size_t SYNC_RATE;
size_t WAIT_RATE;

auto randgen(size_t align = 0)
{
    static const auto size = FILE_SIZE - (READ_SIZE > WRITE_SIZE ? READ_SIZE : WRITE_SIZE) + 1;
    return rand() % size & -align;
}

class RandomWrite;
class AIOWrite;
class BlockingWrite;
class TAIWrite;

class RandomWrite
{
public:
    int fd;
    int openflags;

    RandomWrite() : openflags(O_RDWR) 
    {
        if (RAND_MAX < numeric_limits<int>::max())
        {
            cerr << "RAND_MAX smaller than expected." << endl;
            exit(-1);
        }
    }

    void run_writeonly(size_t thread_id)
    {
        //auto file = fopen(("tmp/file" + to_string(thread_id)).data(), "r+");
        fd = open(("tmp/file" + to_string(thread_id)).data(), openflags); //fileno(file);
        tai::register_fd(fd, "tmp/file" + to_string(thread_id));
        //vector<char> data(WRITE_SIZE, 'a');
        auto data = new(align_val_t(512)) char[WRITE_SIZE];
        memset(data, 'a', sizeof(WRITE_SIZE));
        reset_cb();
        for (size_t i = 0; i < IO_ROUND; ++i)
        {
            if (i && !(i & ~-SYNC_RATE))
            {
                syncop();
                if (!(i & ~-WAIT_RATE))
                    wait_cb();
            }
            auto offset = randgen(max(READ_SIZE, WRITE_SIZE));
            writeop(offset, data);
            if (!i || i * 30 / IO_ROUND > (i - 1) * 30 / IO_ROUND)
                tai::Log::log("[Thread ", thread_id, "]", "Progess ", i * 100 / IO_ROUND, "\% finished.");
        }
        syncop();
        wait_cb();
        cleanup();
        tai::deregister_fd(fd);
        //fclose(file);
        close(fd);
        operator delete[](data, align_val_t(512));
    }

    void run_readonly(size_t thread_id)
    {
        fd = open(("tmp/file" + to_string(thread_id)).data(), openflags); 
        tai::register_fd(fd, "tmp/file" + to_string(thread_id));
        auto buf = new(align_val_t(512)) char[WRITE_SIZE * WAIT_RATE];
        reset_cb();
        for (size_t i = 0; i < IO_ROUND; ++i)
        {
            if (i && !(i & ~-WAIT_RATE))
            {
                wait_cb();
            }
            auto offset = randgen(max(READ_SIZE, WRITE_SIZE));
            readop(offset, buf + (i & ~-WAIT_RATE) * WRITE_SIZE);
            if (!i || i * 30 / IO_ROUND > (i - 1) * 30 / IO_ROUND)
                tai::Log::log("[Thread ", thread_id, "]", "Progess ", i * 100 / IO_ROUND, "\% finished.");
        }
        //syncop();
        wait_cb();
        cleanup();
        tai::deregister_fd(fd);
        close(fd);
        operator delete[](buf, align_val_t(512));
    }

    void run_readwrite(size_t thread_id)
    {
        fd = open(("tmp/file" + to_string(thread_id)).data(), openflags); 
        tai::register_fd(fd, "tmp/file" + to_string(thread_id));
        auto data = new(align_val_t(512)) char[WRITE_SIZE];
        auto buf = new(align_val_t(512)) char[WRITE_SIZE * WAIT_RATE];
        vector<size_t> offs;
        offs.reserve(IO_ROUND);
        memset(data, 'a', sizeof(WRITE_SIZE));
        reset_cb();
        for (size_t i = 0; i < IO_ROUND; ++i)
        {
            if (i && !(i & ~-SYNC_RATE))
            {
                syncop();
                if (!(i & ~-WAIT_RATE))
                {
                    for (auto j = i - WAIT_RATE; j < i; ++j)
                        readop(offs[j], buf + (j & ~-WAIT_RATE) * WRITE_SIZE);
                    wait_cb();
                }
            }
            auto offset = randgen(max(READ_SIZE, WRITE_SIZE));
            offs.push_back(offset);
            writeop(offset, data);
            if (!i || i * 30 / IO_ROUND > (i - 1) * 30 / IO_ROUND)
                tai::Log::log("[Thread ", thread_id, "]", "Progess ", i * 100 / IO_ROUND, "\% finished.");
        }
        for (auto j = IO_ROUND - WAIT_RATE; j < IO_ROUND; ++j)
            readop(offs[j], buf + (j & ~-WAIT_RATE) * WRITE_SIZE);
        syncop();
        wait_cb();
        cleanup();
        tai::deregister_fd(fd);
        close(fd);
        operator delete[](data, align_val_t(512));
        operator delete[](buf, align_val_t(512));
    }

    void run(size_t thread_id)
    {
        array<function<void(size_t)>, 3>{
		[this](auto _){ run_readonly(_); },
		[this](auto _){ run_writeonly(_); },
		[this](auto _){ run_readwrite(_); }
	}[workload](thread_id);
    }

    virtual void writeop(off_t offset, char* data) = 0;
    virtual void readop(off_t offset, char* data) = 0;
    virtual void syncop() = 0;
    virtual void reset_cb() {}
    virtual void wait_cb() {}
    virtual void cleanup() {}
};

class BlockingWrite : public RandomWrite
{
public: 

    BlockingWrite() {}

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

    void readop(off_t offset, char* data) override
    {
        if (lseek(fd, offset, SEEK_SET) < 0)
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at lseek." << endl;
            exit(-1);
        }
        if (read(fd, data, READ_SIZE) < 0)
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at read." << endl;
            exit(-1);
        }
    }

    void syncop() override
    {
        if (fsync(fd))
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at fsync." << endl;
            exit(-1);
        }
    }

    static void startEntry(size_t thread_id, int flags = 0)
    {
        BlockingWrite bw;
        bw.openflags |= flags;
        bw.run(thread_id);
    }
};

class AIOWrite : public RandomWrite
{
public:

    AIOWrite() { cbs.reserve(2 * IO_ROUND + IO_ROUND / SYNC_RATE + 1); }

    vector<aiocb> cbs;

    void reset_cb() override { cbs.clear(); }

    void wait_cb() override
    {
        while (aio_error(&(cbs.back())) == EINPROGRESS)
            sleep_for(1ms);
    }

    void cleanup() override
    {
        for (auto &i : cbs)
            if (unlikely(aio_error(&i) && aio_error(&i) != EINPROGRESS))
            {
                cerr << aio_error(&i) <<  " Error " << errno << ": " << strerror(errno) << " at aio_error." << endl;
                cerr << "reqprio: " << i.aio_reqprio << ", offset: " << i.aio_offset << " , nbytes: " << i.aio_nbytes << endl; 
                exit(-1);
            }
            else
                aio_return(&i);
    }

    void writeop(off_t offset, char* data) override
    {
        cbs.emplace_back();
        auto& cb = cbs.back();
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
    }

    void readop(off_t offset, char* data) override
    {
        cbs.emplace_back();
        auto& cb = cbs.back();
        memset(&cb, 0, sizeof(aiocb));
        cb.aio_fildes = fd;
        cb.aio_nbytes = READ_SIZE;
        cb.aio_buf = data;
        cb.aio_offset = offset;
        if (aio_read(&cb))
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at aio_read." << endl;
            exit(-1);
        }
    }

    void syncop() override
    {
        cbs.emplace_back();
        auto& cb = cbs.back();
        memset(&cb, 0, sizeof(aiocb));
        cb.aio_fildes = fd;
        if (aio_fsync(O_SYNC, &cb))
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at aio_fsync." << endl;
            exit(-1);
        }

    }

    static void startEntry(size_t thread_id)
    {
        AIOWrite aw;
        aw.run(thread_id);
    }
};

class LibAIOWrite : public RandomWrite
{
public:

    LibAIOWrite()
    {
        #ifdef __linux__
        cbs.reserve(2 * IO_ROUND + IO_ROUND / SYNC_RATE + 1); openflags |= O_DIRECT;
        #endif
    }

    #ifdef __linux__
    vector<iocb*> cbs;
    #endif

    void reset_cb() override
    {
        #ifdef __linux__
        cbs.clear();
        #endif
    }

    void wait_cb() override
    {
        #ifdef __linux__
        io_getevents(io_cxt, cnt, cnt, events, NULL);
        #endif
        cnt = 0;
    }

    void writeop(off_t offset, char* data) override
    {
        #ifdef __linux__
        cbs.emplace_back(new iocb);
        auto& cb = cbs.back();
        io_prep_pwrite(cb, fd, data, WRITE_SIZE, offset);
        if (io_submit(io_cxt, 1, &cb) < 1)
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at libaio: write." << endl;
            exit(-1);
        }
        #endif
        ++cnt;
    }

    void readop(off_t offset, char* data) override
    {
        #ifdef __linux__
        cbs.emplace_back(new iocb);
        auto& cb = cbs.back();
        io_prep_pread(cb, fd, data, READ_SIZE, offset);
        if (io_submit(io_cxt, 1, &cb) < 1)
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at libaio: read." << endl;
            exit(-1);
        }
        #endif
        ++cnt;
    }

    void syncop() override
    {
       //    cbs.emplace_back(new iocb);
       //    auto& cb = cbs.back();
       //    int err;
       //    //io_prep_fsync(cb, fd);
       //    //if ((err = io_submit(io_cxt, 1, &cb)) < 0)
       //    if ((err = io_fsync(io_cxt, cb, nullptr, fd)) < 0)
       //    {
       //        cerr << "Error " << errno << ": " << strerror(errno) << " at libaio: fsync." << endl;
       //        cerr << "return: " << err << endl;
       //        exit(-1);
       //    }
       //    ++cnt;
    }

    static void startEntry(size_t thread_id)
    {
        LibAIOWrite lw;
        lw.run(thread_id);
    }

    int cnt = 0;

    #ifdef __linux__
    io_event events[65536];
    static io_context_t io_cxt;
    #endif
};

#ifdef __linux__
io_context_t LibAIOWrite::io_cxt;
#endif

class TAIWrite : public RandomWrite
{
public: 

    TAIWrite() { cbs.reserve(2 * IO_ROUND + IO_ROUND / SYNC_RATE + 1); }

    vector<tai::aiocb> cbs;

    void reset_cb() override { cbs.clear(); }

    void wait_cb() override
    {
        while (tai::aio_error(&(cbs.back())) == EINPROGRESS)
            sleep_for(1ms);
    }

    void cleanup() override
    {
        for (auto &i : cbs)
            if (unlikely(tai::aio_error(&i) && tai::aio_error(&i) != EINPROGRESS))
            {
                cerr << tai::aio_error(&i) <<  " Error " << errno << ": " << strerror(errno) << " at aio_error." << endl;
                cerr << "reqprio: " << i.aio_reqprio << ", offset: " << i.aio_offset << " , nbytes: " << i.aio_nbytes << endl; 
                exit(-1);
            }
            else
                tai::aio_return(&i);
        if (fsync(fd))
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at fsync." << endl;
            exit(-1);
        }
    }

    void writeop(off_t offset, char* data) override
    {
        cbs.emplace_back(fd, offset, data, WRITE_SIZE);
        if (tai::aio_write(&cbs.back()))
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at aio_write." << endl;
            exit(-1);
        }
    }

    void readop(off_t offset, char* data) override
    {
        cbs.emplace_back(fd, offset, data, READ_SIZE);
        if (tai::aio_read(&cbs.back()))
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at aio_read." << endl;
            exit(-1);
        }
    }

    void syncop() override
    {
        cbs.emplace_back(fd);
        if (tai::aio_fsync(O_SYNC, &cbs.back()))
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at aio_fsync." << endl;
            exit(-1);
        }
    }

    static void startEntry(size_t thread_id)
    {
        TAIWrite tw;
        tw.run(thread_id);
    }
};

int main(int argc, char* argv[])
{
    if (argc != 10)
    {
        cerr << "Need arguments for thread number, type of IO to test and workload type" << endl;
        exit(-1);
    }
    [&](vector<size_t*> _){ for (auto i = _.size(); i--; *_[i] = stoll(argv[i + 1])); }({
            &thread_num,
            &testType,
            &workload
            });
    [&](vector<size_t*> _){ for (auto i = _.size(); i--; *_[i] = 1ll << stoll(argv[i + 4])); }({
            &FILE_SIZE,
            &READ_SIZE,
            &WRITE_SIZE,
            &IO_ROUND,
            &SYNC_RATE,
            &WAIT_RATE
            });

    tai::Log::log("thread number: ", thread_num);
    srand(time(nullptr));
    system("sync");

    string testname[] = {"Blocking IO", "Blocking IO (O_DIRECT | O_SYNC)", "Async IO", "LibAIO", "TAI"};
    string wlname[] = {"read", "write", "read&write"};
    auto begin = high_resolution_clock::now();

    tai::Log::log(testname[testType]);
    vector<thread> threads;
    switch (testType)
    {
    case 0:
    case 1:
        for (size_t i = 0; i < thread_num; ++i)
            threads.emplace_back(thread(BlockingWrite::startEntry, i, testType & (
                            #ifdef __linux__
                            O_DIRECT |
                            #endif
                            O_SYNC)));
        break;
    case 2:
        for (size_t i = 0; i < thread_num; ++i)
            threads.emplace_back(thread(AIOWrite::startEntry, i));
        break;
    case 3:
        #ifdef __linux__
        io_setup(1048576, &LibAIOWrite::io_cxt);
        #endif
        for (size_t i = 0; i < thread_num; ++i)
            threads.emplace_back(thread(LibAIOWrite::startEntry, i));
        break;
    case 4:
        tai::aio_init();
        for (size_t i = 0; i < thread_num; ++i)
            threads.emplace_back(thread(TAIWrite::startEntry, i));
        break;
    }
    for (auto& t : threads)
	t.join();

    auto time = duration_cast<nanoseconds>(high_resolution_clock::now() - begin).count();
    if (testType == 4)
        tai::aio_end();
    tai::Log::log(testname[testType], " random ", wlname[workload], ": ", time / 1e9, " s in total, ", IO_ROUND * (int(workload == 2) + 1), " ops/thread, ",
            thread_num, " threads, ", 1e9 * IO_ROUND * (int(workload == 2) + 1) * thread_num / time, " iops");
    if (testType == 4)
    {
        auto rt = tai::Log::run_time.load() / 1e9;
        auto st = tai::Log::steal_time.load() / 1e9;
        auto bt = tai::Log::barrier_time.load() / 1e9;
        tai::Log::log("Total run time: " , rt, " s, steal time: ", st, " s, barrier time: ", bt, " s");
    }

    return 0;
}
