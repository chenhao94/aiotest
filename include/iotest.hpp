#include <iostream>
#include <ctime>
#include <chrono>
#include <thread>
#include <array>
#include <vector>
#include <limits>
#include <numeric>
#include <functional>
#include <new>
#include <string>
#include <memory>
#include <mutex>
#include <atomic>

#if defined(__unix__) || defined(__MACH__)
#include <unistd.h>
#endif

#ifdef _POSIX_VERSION
#include <fcntl.h>
#include <errno.h>
#include <aio.h>
#endif

#ifdef __linux__
#include <sys/wait.h>
#include <libaio.h>
#endif

#include "tai.hpp"
#include "aio.hpp"

#define unlikely(x)     __builtin_expect((x),0)

static const std::string testname[] = {"Posix", "DIO", "PosixAIO", "LibAIO", "TaiAIO", "STL", "Tai"};
static const std::string wlname[] = {"read", "write", "read&write"};

extern size_t testType;
extern size_t FILE_SIZE;
extern size_t WRITE_SIZE;
extern size_t IO_ROUND;

extern size_t thread_num;
extern size_t testType;
extern size_t workload;
extern size_t FILE_SIZE;
extern size_t READ_SIZE;
extern size_t WRITE_SIZE;
extern size_t IO_ROUND;
extern size_t SYNC_RATE;
extern size_t WAIT_RATE;

extern bool SINGLE_FILE;

static constexpr size_t MAX_THREAD_NUM = 8;

static void processArgs(int argc, char* argv[])
{
    using namespace std;
    using namespace tai;

    if (argc < 10)
    {
        cerr << "Need arguments for thread number, type of IO to test and workload type" << endl;
        exit(-1);
    }
    [&](vector<size_t*> _){ for (auto i = _.size(); i--; *_[i] = stoll(argv[i + 1])); }({
            &thread_num,
            &testType,
            &workload,
            });
    [&](vector<size_t*> _){ for (auto i = _.size(); i--; *_[i] = 1ll << stoll(argv[i + 4])); }({
            &FILE_SIZE,
            });
    [&](vector<size_t*> _){ for (auto i = _.size(); i--; *_[i] = stoll(argv[i + 5])); }({
            &READ_SIZE,
            &WRITE_SIZE,
            });
    [&](vector<size_t*> _){ for (auto i = _.size(); i--; *_[i] = 1ll << stoll(argv[i + 7])); }({
            &IO_ROUND,
            &SYNC_RATE,
            &WAIT_RATE
            });
    if (argc > 10)
        SINGLE_FILE = (stoll(argv[10]) > 0);

    READ_SIZE = READ_SIZE << 10;
    WRITE_SIZE = WRITE_SIZE << 10;

    Log::log("thread number: ", thread_num);
    Log::log(testname[testType], " on ", SINGLE_FILE ? "single file" : "multiple files");
}

TAI_INLINE
static auto randgen(size_t align = 0)
{
    static const auto size = FILE_SIZE - (READ_SIZE > WRITE_SIZE ? READ_SIZE : WRITE_SIZE) + 1;
    return rand() % size & -align;
}

class RandomWrite
{
public:
    int fd = -1;
    int openflags;
    std::atomic<size_t> opencnt = 0;

    RandomWrite()
    {
        using namespace std;

        #ifdef _POSIX_VERSION
        openflags = O_RDWR;
        #endif

        if (RAND_MAX < numeric_limits<int>::max())
        {
            cerr << "RAND_MAX smaller than expected." << endl;
            exit(-1);
        }
    }

    virtual ~RandomWrite() {}

    void run_writeonly()
    {
        using namespace std;
        using namespace tai;

        openfile("tmp/file" + to_string(SINGLE_FILE ? 0 : tid));
        auto data = new
            #ifndef __MACH__
            (align_val_t(4096))
            #endif
            char[WRITE_SIZE];
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
            auto offset = randgen(WRITE_SIZE);
            writeop(offset, data);
            if (!i || i * 10 / IO_ROUND > (i - 1) * 10 / IO_ROUND)
                Log::log("[Thread ", tid, "]", "Progess ", i * 100 / IO_ROUND, "\% finished.");
        }
        syncop();
        wait_cb();
        cleanup();
        closefile();
        operator delete[](data
                #ifndef __MACH__
                , align_val_t(4096)
                #endif
                );
    }

    TAI_INLINE
    void run_readonly()
    {
        using namespace std;
        using namespace tai;

        openfile("tmp/file" + to_string(SINGLE_FILE ? 0 : tid));
        auto buf = new
            #ifndef __MACH__
            (align_val_t(4096))
            #endif
            char[READ_SIZE * WAIT_RATE];
        reset_cb();
        for (size_t i = 0; i < IO_ROUND; ++i)
        {
            if (i && !(i & ~-WAIT_RATE))
                wait_cb();
            auto offset = randgen(READ_SIZE);
            //readop(offset, buf + (i & ~-WAIT_RATE) * WRITE_SIZE);
            readop(offset, buf);
            if (!i || i * 10 / IO_ROUND > (i - 1) * 10 / IO_ROUND)
                Log::log("[Thread ", tid, "]", "Progess ", i * 100 / IO_ROUND, "\% finished.");
        }
        //syncop();
        wait_cb();
        cleanup();
        closefile();
        operator delete[](buf
                #ifndef __MACH__
                , align_val_t(4096)
                #endif
                );
    }

    TAI_INLINE
    void run_readwrite()
    {
        using namespace std;
        using namespace tai;

        openfile("tmp/file" + to_string(SINGLE_FILE ? 0 : tid));
        auto data = new
            #ifndef __MACH__
            (align_val_t(4096))
            #endif
            char[WRITE_SIZE];
        auto buf = new
            #ifndef __MACH__
            (align_val_t(4096))
            #endif
            char[WRITE_SIZE * WAIT_RATE];
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
            writeop(offs.emplace_back(randgen(WRITE_SIZE)), data);
            if (!i || i * 10 / IO_ROUND > (i - 1) * 10 / IO_ROUND)
                Log::log("[Thread ", tid, "]", "Progess ", i * 100 / IO_ROUND, "\% finished.");
        }
        for (auto j = IO_ROUND - WAIT_RATE; j < IO_ROUND; ++j)
            readop(offs[j], buf + (j & ~-WAIT_RATE) * WRITE_SIZE);
        syncop();
        wait_cb();
        cleanup();
        closefile();
        operator delete[](data
                #ifndef __MACH__
                , align_val_t(4096)
                #endif
                );
        operator delete[](buf
                #ifndef __MACH__
                , align_val_t(4096)
                #endif
                );
    }

    TAI_INLINE
    void run(ssize_t tid)
    {
        using namespace std;

        this->tid = tid;

        array<function<void()>, 3>{
            [this](){ run_readonly(); },
            [this](){ run_writeonly(); },
            [this](){ run_readwrite(); }
        }[workload]();
    }

    TAI_INLINE
    virtual void openfile(const std::string& filename)
    {
        using namespace std;

        if (opencnt.fetch_add(1))
            return;

        #ifdef _POSIX_VERSION
        fd = open(filename.c_str(), openflags);
        #else
        cerr << "RandomWrite::openfile() needs POSIX support." << endl;
        #endif
    }

    TAI_INLINE
    virtual void closefile()
    {
        using namespace std;

        if (opencnt.fetch_sub(1) - 1)
            return;

        #ifdef _POSIX_VERSION
        close(fd);
        fd = -1;
        #else
        cerr << "RandomWrite::closefile() needs POSIX support." << endl;
        #endif
    }

    virtual void writeop(off_t offset, char* data) = 0;
    virtual void readop(off_t offset, char* data) = 0;
    virtual void syncop() = 0;

    TAI_INLINE
    virtual void reset_cb() {}

    TAI_INLINE
    virtual void wait_cb() {}

    TAI_INLINE
    virtual void busywait_cb() {}

    TAI_INLINE
    virtual void cleanup() {}

    static std::unique_ptr<RandomWrite> getInstance(int testType, bool concurrent = false);
    static thread_local ssize_t tid;
};

class BlockingWrite : public RandomWrite
{
public: 
    TAI_INLINE
    BlockingWrite()
    {
    }

    TAI_INLINE
    virtual void writeop(off_t offset, char* data) override
    {
        using namespace std;

        #ifdef _POSIX_VERSION
        if (pwrite(fd, data, WRITE_SIZE, offset) < 0)
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at pwrite." << endl;
            exit(-1);
        }
        #else
        cerr << "Warning: BlockingWrite needs POSIX support." << endl;
        #endif
    }

    TAI_INLINE
    virtual void readop(off_t offset, char* data) override
    {
        using namespace std;

        #ifdef _POSIX_VERSION
        if (pread(fd, data, READ_SIZE, offset) < 0)
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at pread." << endl;
            exit(-1);
        }
        #else
        cerr << "Warning: BlockingWrite needs POSIX support." << endl;
        #endif
    }

    TAI_INLINE
    virtual void syncop() override
    {
        using namespace std;

        #ifdef _POSIX_VERSION
        if (fsync(fd))
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at fsync." << endl;
            exit(-1);
        }
        #else
        cerr << "Warning: BlockingWrite needs POSIX support." << endl;
        #endif
    }

    TAI_INLINE
    static void startEntry(size_t thread_id, int flags = 0)
    {
        using namespace std;

        #ifdef _POSIX_VERSION
        BlockingWrite bw;
        bw.openflags |= flags;
        bw.run(thread_id);
        #else
        cerr << "Warning: BlockingWrite needs POSIX support." << endl;
        #endif
    }
};

template <bool concurrent = false>
class FstreamWrite : public BlockingWrite
{
    std::fstream file;
    std::mutex writeLock;

public: 
    TAI_INLINE
    FstreamWrite()
    {
    }

    TAI_INLINE
    virtual void writeop(off_t offset, char* data) override
    {
        if (concurrent)
            writeLock.lock();
        file.seekp(offset).write(data, WRITE_SIZE);
        if (concurrent)
            writeLock.unlock();
    }

    TAI_INLINE
    virtual void readop(off_t offset, char* data) override
    {
        if (concurrent)
            writeLock.lock();
        file.seekg(offset).read(data, READ_SIZE);
        if (concurrent)
            writeLock.lock();
    }

    TAI_INLINE
    virtual void syncop() override
    {
        file.flush();
        BlockingWrite::syncop();
    }

    TAI_INLINE
    virtual void openfile(const std::string& filename) override
    {
        using namespace std;

        if (opencnt.fetch_add(1))
            return;

        #ifdef _POSIX_VERSION
        fd = open(filename.c_str(), openflags); // fd for fsync
        #endif

        if (file.is_open())
            file.close();
        file.open(filename, ios::binary | ios::in | ios::out);
    }

    virtual void closefile() override
    {
        if (opencnt.fetch_sub(1) - 1)
            return;

        file.close();

        #ifdef _POSIX_VERSION
        close(fd);
        #endif
    }

    TAI_INLINE
    static void startEntry(size_t thread_id)
    {
        FstreamWrite<concurrent> fw;
        fw.run(thread_id);
    }
};

class AIOWrite : public RandomWrite
{
    #ifdef _POSIX_VERSION
    std::array<std::vector<aiocb>, MAX_THREAD_NUM> cbs;
    #endif

public:
    TAI_INLINE
    AIOWrite()
    {
        using namespace std;

        #ifdef _POSIX_VERSION
        for (auto& i : cbs)
            i.reserve(2 * IO_ROUND + IO_ROUND / SYNC_RATE + 1);
        #else
        cerr << "Warning: POSIX AIO needs POSIX support." << endl;
        #endif
    }

    TAI_INLINE
    virtual void reset_cb() override
    {
        using namespace std;

        #ifdef _POSIX_VERSION
        cbs[tid].clear();
        #else
        cerr << "Warning: POSIX AIO needs POSIX support." << endl;
        #endif
    }

    TAI_INLINE
    virtual void wait_cb() override
    {
        using namespace std;
        using namespace chrono_literals;

        #ifdef _POSIX_VERSION
        for (; aio_error(&(cbs[tid].back())) == EINPROGRESS; this_thread::sleep_for(1ms));
        #else
        cerr << "Warning: POSIX AIO needs POSIX support." << endl;
        #endif
    }

    TAI_INLINE
    virtual void busywait_cb() override
    {
        using namespace std;

        #ifdef _POSIX_VERSION
        while (aio_error(&(cbs[tid].back())) == EINPROGRESS);
        #else
        cerr << "Warning: POSIX AIO needs POSIX support." << endl;
        #endif
    }

    TAI_INLINE
    virtual void cleanup() override
    {
        using namespace std;

        #ifdef _POSIX_VERSION
        for (auto &i : cbs[tid])
            if (unlikely(aio_error(&i) && aio_error(&i) != EINPROGRESS))
            {
                cerr << aio_error(&i) <<  " Error " << errno << ": " << strerror(errno) << " at aio_error." << endl;
                cerr << "reqprio: " << i.aio_reqprio << ", offset: " << i.aio_offset << " , nbytes: " << i.aio_nbytes << endl; 
                exit(-1);
            }
            else
                aio_return(&i);
        #else
        cerr << "Warning: POSIX AIO needs POSIX support." << endl;
        #endif
    }


    TAI_INLINE
    virtual void writeop(off_t offset, char* data) override
    {
        using namespace std;

        #ifdef _POSIX_VERSION
        auto& cb = cbs[tid].emplace_back();
        memset(&cb, 0, sizeof(cb));
        cb.aio_fildes = fd;
        cb.aio_nbytes = WRITE_SIZE;
        cb.aio_buf = data;
        cb.aio_offset = offset;
        if (aio_write(&cb))
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at aio_write." << endl;
            exit(-1);
        }
        #else
        cerr << "Warning: POSIX AIO needs POSIX support." << endl;
        #endif
    }

    TAI_INLINE
    virtual void readop(off_t offset, char* data) override
    {
        using namespace std;

        #ifdef _POSIX_VERSION
        auto& cb = cbs[tid].emplace_back();
        memset(&cb, 0, sizeof(cb));
        cb.aio_fildes = fd;
        cb.aio_nbytes = READ_SIZE;
        cb.aio_buf = data;
        cb.aio_offset = offset;
        if (aio_read(&cb))
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at aio_read." << endl;
            exit(-1);
        }
        #else
        cerr << "Warning: POSIX AIO needs POSIX support." << endl;
        #endif
    }


    TAI_INLINE
    virtual void syncop() override
    {
        using namespace std;

        #ifdef _POSIX_VERSION
        auto& cb = cbs[tid].emplace_back();
        memset(&cb, 0, sizeof(cb));
        cb.aio_fildes = fd;
        if (aio_fsync(O_SYNC, &cb))
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at aio_fsync." << endl;
            exit(-1);
        }
        #else
        cerr << "Warning: POSIX AIO needs POSIX support." << endl;
        #endif
    }

    TAI_INLINE
    static void startEntry(size_t thread_id)
    {
        using namespace std;

        #ifdef _POSIX_VERSION
        AIOWrite aw;
        aw.run(thread_id);
        #else
        cerr << "Warning: POSIX AIO needs POSIX support." << endl;
        #endif
    }

};

class LibAIOWrite : public RandomWrite
{
    #ifdef __linux__
    std::array<std::vector<iocb*>, MAX_THREAD_NUM> cbs;
    io_event events[MAX_THREAD_NUM][65536];
    static io_context_t io_cxt;
    #endif

    static std::mutex cntMtx;
    static size_t cnt;

public:
    TAI_INLINE
    LibAIOWrite()
    {
        using namespace std;

        #ifdef __linux__
        for (auto& i : cbs)
            i.reserve(2 * IO_ROUND + IO_ROUND / SYNC_RATE + 1);
        openflags |= O_DIRECT;
        #else
        cerr << "Warning: LibAIO is not supported on non-Linux system." << endl;
        #endif
    }

    TAI_INLINE
    virtual void reset_cb() override
    {
        using namespace std;

        #ifdef __linux__
        cbs[tid].clear();
        #else
        cerr << "Warning: LibAIO is not supported on non-Linux system." << endl;
        #endif
    }

    TAI_INLINE
    virtual void wait_cb() override
    {
        using namespace std;

        unique_lock<mutex> lck(cntMtx);

        #ifdef __linux__
        io_getevents(io_cxt, cnt, cnt, events[tid], nullptr);
        #else
        cerr << "Warning: LibAIO is not supported on non-Linux system." << endl;
        #endif

        cnt = 0;
    }

    // No way to busy wait.
    TAI_INLINE
    virtual void busywait_cb() override
    {
        wait_cb();
    }

    TAI_INLINE
    virtual void writeop(off_t offset, char* data) override
    {
        using namespace std;

        #ifdef __linux__
        auto& cb = cbs[tid].emplace_back(new iocb);
        io_prep_pwrite(cb, fd, data, WRITE_SIZE, offset);
        auto err = io_submit(io_cxt, 1, &cb);
        if (err < 1)
        {
            cerr << "Error " << -err << ": " << strerror(-err) << " at libaio: write." << endl;
            exit(-1);
        }
        #else
        cerr << "Warning: LibAIO is not supported on non-Linux system." << endl;
        #endif

        unique_lock<mutex> lck(cntMtx);
        ++cnt;
    }

    TAI_INLINE
    virtual void readop(off_t offset, char* data) override
    {
        using namespace std;

        #ifdef __linux__
        auto& cb = cbs[tid].emplace_back(new iocb);
        io_prep_pread(cb, fd, data, READ_SIZE, offset);
        auto err = io_submit(io_cxt, 1, &cb);
        if (err < 1)
        {
            cerr << "Error " << -err << ": " << strerror(-err) << " at libaio: read." << endl;
            exit(-1);
        }
        #else
        cerr << "Warning: LibAIO is not supported on non-Linux system." << endl;
        #endif

        unique_lock<mutex> lck(cntMtx);
        ++cnt;
    }

    TAI_INLINE
    virtual void syncop() override
    {
        using namespace std;

        wait_cb();

        #ifdef _POSIX_VERSION
        if (fsync(fd))
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at fsync." << endl;
            exit(-1);
        }
        #else
        cerr << "Warning: LibAIOWrite needs POSIX support." << endl;
        #endif
    }

    TAI_INLINE
    static void startEntry(size_t thread_id)
    {
        using namespace std;

        #ifdef __linux__
        LibAIOWrite lw;
        lw.run(thread_id);
        #else
        cerr << "Warning: LibAIO is not supported on non-Linux system." << endl;
        #endif
    }
};

class TAIAIOWrite : public RandomWrite
{
    std::array<std::vector<tai::aiocb, tai::Alloc<tai::aiocb>>, MAX_THREAD_NUM> cbs;

public: 
    TAI_INLINE
    TAIAIOWrite()
    {
        for (auto& i : cbs)
            i.reserve(2 * IO_ROUND + IO_ROUND / SYNC_RATE + 1);
    }

    TAI_INLINE
    virtual void reset_cb() override
    {
        cbs[tid].clear();
    }

    TAI_INLINE
    virtual void wait_cb() override
    {
        using namespace tai;

        for (auto& i : cbs[tid])
            aio_wait(&i);
    }

    TAI_INLINE
    virtual void busywait_cb() override
    {
        using namespace tai;

        for (auto& i : cbs[tid])
            while (aio_error(&i) == EINPROGRESS);
    }

    TAI_INLINE
    virtual void cleanup() override
    {
        using namespace std;
        using namespace tai;

        for (auto &i : cbs[tid])
            if (unlikely(aio_error(&i) && aio_error(&i) != EINPROGRESS))
            {
                cerr << aio_error(&i) <<  " Error " << errno << ": " << strerror(errno) << " at aio_error." << endl;
                cerr << "reqprio: " << i.aio_reqprio << ", offset: " << i.aio_offset << " , nbytes: " << i.aio_nbytes << endl; 
                exit(-1);
            }
            else
                aio_return(&i);
        cbs[tid].clear();
        #ifdef _POSIX_VERSION
        if (fsync(fd))
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at fsync." << endl;
            exit(-1);
        }
        #endif
    }

    TAI_INLINE
    virtual void writeop(off_t offset, char* data) override
    {
        using namespace std;
        using namespace tai;

        if (aio_write(&cbs[tid].emplace_back(fd, offset, data, WRITE_SIZE)))
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at tai::aio_write." << endl;
            exit(-1);
        }
    }

    TAI_INLINE
    virtual void readop(off_t offset, char* data) override
    {
        using namespace std;
        using namespace tai;

        if (aio_read(&cbs[tid].emplace_back(fd, offset, data, READ_SIZE)))
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at tai::aio_read." << endl;
            exit(-1);
        }
    }

    TAI_INLINE
    virtual void syncop() override
    {
        using namespace std;
        using namespace tai;

        #ifndef _POSIX_VERSION
        auto O_SYNC = 0;
        #endif
        if (aio_fsync(O_SYNC, &cbs[tid].emplace_back(fd)))
        {
            cerr << "Error " << errno << ": " << strerror(errno) << " at tai::aio_fsync." << endl;
            exit(-1);
        }
    }

    TAI_INLINE
    virtual void openfile(const std::string& filename) override
    {
        using namespace std;
        using namespace tai;

        if (opencnt.fetch_add(1) > 0)
            return;
        #ifdef _POSIX_VERSION
        fd = open(filename.c_str(), openflags);
        register_fd(fd, filename);
        #else
        cerr << "RandomWrite::openfile() needs POSIX support." << endl;
        #endif
    }

    TAI_INLINE
    virtual void closefile() override
    {
        using namespace std;
        using namespace tai;

        if (opencnt.fetch_sub(1) > 1)
            return;
        deregister_fd(fd);
        #ifdef _POSIX_VERSION
        close(fd);
        fd = -1;
        #else
        cerr << "RandomWrite::closefile() needs POSIX support." << endl;
        #endif
    }

    TAI_INLINE
    static void startEntry(size_t thread_id)
    {
        TAIAIOWrite tw;
        tw.run(thread_id);
    }
};

class TAIWrite : public RandomWrite
{
    std::unique_ptr<tai::BTreeBase> bt;
    std::array<tai::IOCtrlVec, MAX_THREAD_NUM> ios;

    static std::unique_ptr<tai::Controller> ctrl;

public: 
    TAI_INLINE
    TAIWrite()
    {
        using namespace std;
        using namespace tai;

        static atomic_flag master = ATOMIC_FLAG_INIT;
        static atomic<bool> slave(false);

        if (!master.test_and_set(memory_order_acq_rel))
        {
            ctrl.reset(new Controller(1ll << 30, 1ll << 32, -1));
            slave.store(true, memory_order_release);
        }
        while (!slave.load(memory_order_acquire));
    }

    TAI_INLINE
    virtual void openfile(const std::string& filename) override 
    {
        using namespace std;
        using namespace tai;

        if (opencnt.fetch_add(1) > 0)
            return;
        ios[tid].reserve(2 * IO_ROUND + IO_ROUND / SYNC_RATE + 1);
        bt.reset(new BTree<44, 4, 4, 12>(new POSIXEngine(filename, O_CREAT | O_RDWR
                        #ifdef __linux__
                        | O_DIRECT
                        #endif
                        )));
    }

    TAI_INLINE
    virtual void closefile() override
    {
        using namespace std;
        using namespace tai;

        syncop();
        ios[tid].back()->wait();
        if (opencnt.fetch_sub(1) > 1)
            return;
        bt->detach(*ctrl);
        bt.reset(nullptr);
        cleanup();
    }

    TAI_INLINE
    virtual void reset_cb() override
    {
        ios[tid].clear();
    }

    TAI_INLINE
    virtual void wait_cb() override
    {
        using namespace std;
        using namespace chrono_literals;

        for (auto& i : ios[tid])
            i->wait(32ms);
        reset_cb();
    }

    TAI_INLINE
    virtual void busywait_cb() override
    {
        using namespace tai;

        for (auto& i : ios[tid])
            while ((*i)() == IOCtrl::Running);
        reset_cb();
    }

    TAI_INLINE
    virtual void writeop(off_t offset, char* data) override
    {
        using namespace std;
        using namespace tai;

        if (unlikely((*ios[tid].emplace_back(bt->write(*ctrl, offset, READ_SIZE, data)))() == IOCtrl::Rejected))
        {
            cerr << "Error at TAI write." << endl;
            exit(-1);
        }
    }

    TAI_INLINE
    virtual void readop(off_t offset, char* data) override
    {
        using namespace std;
        using namespace tai;

        if (unlikely((*ios[tid].emplace_back(bt->readsome(*ctrl, offset, READ_SIZE, data)))() == IOCtrl::Rejected))
        {
            cerr << "Error at TAI read." << endl;
            exit(-1);
        }
    }

    TAI_INLINE
    virtual void syncop() override
    {
        using namespace std;
        using namespace tai;

        if (unlikely((*ios[tid].emplace_back(bt->fsync(*ctrl)))() == IOCtrl::Rejected))
        {
            cerr << "Error at TAI sync." << endl;
            exit(-1);
        }
    }

    TAI_INLINE
    virtual void cleanup() override
    {
        wait_cb();
    }

    TAI_INLINE
    static void end()
    {
        ctrl = nullptr;
    }
};

