#include <iostream>
#include <ctime>
#include <chrono>
#include <thread>
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

extern std::string testname[];
extern std::string wlname[];

void processArgs(int argc, char* argv[]);

inline auto randgen(size_t align = 0)
{
    static const auto size = FILE_SIZE - (READ_SIZE > WRITE_SIZE ? READ_SIZE : WRITE_SIZE) + 1;
    return rand() % size & -align;
}

class RandomWrite;
class AIOWrite;
class BlockingWrite;
class TAIAIOWrite;

class RandomWrite
{
public:
    int fd = -1;
    int openflags;
    std::atomic_int opencnt = 0;

    RandomWrite()
    {
        #ifdef _POSIX_VERSION
        openflags = O_RDWR;
        #endif

        if (RAND_MAX < std::numeric_limits<int>::max())
        {
            std::cerr << "RAND_MAX smaller than expected." << std::endl;
            exit(-1);
        }
    }

    void run_writeonly(size_t thread_id);
    void run_readonly(size_t thread_id);
    void run_readwrite(size_t thread_id);
    void run(size_t thread_id);

    virtual void openfile(const std::string& filename)
    {
        if (opencnt.fetch_add(1) > 0)
            return;
        #ifdef _POSIX_VERSION
        fd = open(filename.c_str(), openflags);
        #else
        std::cerr << "RandomWrite::openfile() needs POSIX support." << std::endl;
        #endif
    }

    virtual void closefile()
    {
        if (opencnt.fetch_sub(1) > 1)
            return;
        #ifdef _POSIX_VERSION
        close(fd);
        fd = -1;
        #else
        std::cerr << "RandomWrite::closefile() needs POSIX support." << std::endl;
        #endif
    }

    virtual void writeop(off_t offset, char* data) = 0;
    virtual void readop(off_t offset, char* data) = 0;
    virtual void syncop() = 0;
    virtual void reset_cb() {}
    virtual void wait_cb() {}
    virtual void busywait_cb() {}
    virtual void cleanup() {}

    static std::unique_ptr<RandomWrite> getInstance(int testType, bool concurrent = false);
    static thread_local int tid;
};

class BlockingWrite : public RandomWrite
{
public: 
    BlockingWrite() {}

    virtual void writeop(off_t offset, char* data) override;
    virtual void readop(off_t offset, char* data) override;
    virtual void syncop() override;
    static void startEntry(size_t thread_id, int flags = 0);
};

template <bool concurrent = false>
class FstreamWrite : public BlockingWrite
{
public: 
    FstreamWrite() {}

    void writeop(off_t offset, char* data) override
    {
        if (concurrent) writeLock.lock();
        file.seekp(offset).write(data, WRITE_SIZE);
        if (concurrent) writeLock.unlock();
    }

    void readop(off_t offset, char* data) override
    {
        if (concurrent) writeLock.lock();
        file.seekg(offset).read(data, READ_SIZE);
        if (concurrent) writeLock.unlock();
    }

    void syncop() override
    {
        file.flush();
        BlockingWrite::syncop();
    }

    virtual void openfile(const std::string& filename) override
    {
        if (opencnt.fetch_add(1) > 0)
            return;
        #ifdef _POSIX_VERSION
        fd = open(filename.c_str(), openflags); // fd for fsync
        #endif
        if (file.is_open())
            file.close();
        file.open(filename, std::ios::binary | std::ios::in | std::ios::out);
    }

    virtual void closefile() override
    {
        if (opencnt.fetch_sub(1) > 1)
            return;
        file.close();
        #ifdef _POSIX_VERSION
        close(fd);
        #endif
    }

    static void startEntry(size_t thread_id);

private:
    std::fstream file;
    std::mutex writeLock;
};

class AIOWrite : public RandomWrite
{
public:
    AIOWrite()
    {
        #ifdef _POSIX_VERSION
        cbs[tid].reserve(2 * IO_ROUND + IO_ROUND / SYNC_RATE + 1);
        #else
        std::cerr << "Warning: POSIX AIO needs POSIX support." << std::endl;
        #endif
    }

    #ifdef _POSIX_VERSION
    std::array<std::vector<aiocb>, MAX_THREAD_NUM> cbs;
    #endif

    inline void reset_cb() override
    {
        #ifdef _POSIX_VERSION
        cbs[tid].clear();
        #else
        std::cerr << "Warning: POSIX AIO needs POSIX support." << std::endl;
        #endif
    }

    inline void wait_cb() override
    {
        using namespace std::chrono_literals;

        #ifdef _POSIX_VERSION
        for (; aio_error(&(cbs[tid].back())) == EINPROGRESS; std::this_thread::sleep_for(1ms));
        #else
        std::cerr << "Warning: POSIX AIO needs POSIX support." << std::endl;
        #endif
    }

    inline void busywait_cb() override
    {
        #ifdef _POSIX_VERSION
        while (aio_error(&(cbs[tid].back())) == EINPROGRESS);
        #else
        std::cerr << "Warning: POSIX AIO needs POSIX support." << std::endl;
        #endif
    }

    void cleanup() override;
    void writeop(off_t offset, char* data) override;
    void readop(off_t offset, char* data) override;
    void syncop() override;
    static void startEntry(size_t thread_id);
};

class LibAIOWrite : public RandomWrite
{
public:
    LibAIOWrite()
    {
        #ifdef __linux__
        cbs[tid].reserve(2 * IO_ROUND + IO_ROUND / SYNC_RATE + 1);
        openflags |= O_DIRECT;
        #else
        std::cerr << "Warning: LibAIO is not supported on non-Linux system." << std::endl;
        #endif
    }

    #ifdef __linux__
    std::array<std::vector<iocb*>, MAX_THREAD_NUM> cbs;
    io_event events[MAX_THREAD_NUM][65536];
    static io_context_t io_cxt;
    #endif

    void reset_cb() override;
    void wait_cb() override;
    void busywait_cb() override { wait_cb(); } // no way to busywait
    void writeop(off_t offset, char* data) override;
    void readop(off_t offset, char* data) override;
    void syncop() override;
    static void startEntry(size_t thread_id);

    std::atomic<int> cnt = 0;
};

class TAIAIOWrite : public RandomWrite
{
public: 
    TAIAIOWrite()
    {
        cbs[tid].reserve(2 * IO_ROUND + IO_ROUND / SYNC_RATE + 1);
    }

    std::array<std::vector<tai::aiocb, tai::Alloc<tai::aiocb>>, MAX_THREAD_NUM> cbs;

    inline void reset_cb() override
    {
        cbs[tid].clear();
    }

    inline void wait_cb() override
    {
        for (auto& i : cbs[tid])
            tai::aio_wait(&i);
    }

    inline void busywait_cb() override
    {
        for (auto& i : cbs[tid])
            while (tai::aio_error(&i) == EINPROGRESS);
    }

    void cleanup() override;
    void writeop(off_t offset, char* data) override;
    void readop(off_t offset, char* data) override;
    void syncop() override;

    virtual void openfile(const std::string& filename) override
    {
        if (opencnt.fetch_add(1) > 0)
            return;
        #ifdef _POSIX_VERSION
        fd = open(filename.c_str(), openflags);
        tai::register_fd(fd, filename);
        #else
        std::cerr << "RandomWrite::openfile() needs POSIX support." << std::endl;
        #endif
    }

    virtual void closefile() override
    {
        if (opencnt.fetch_sub(1) > 1)
            return;
        tai::deregister_fd(fd);
        #ifdef _POSIX_VERSION
        close(fd);
        fd = -1;
        #else
        std::cerr << "RandomWrite::closefile() needs POSIX support." << std::endl;
        #endif
    }

    static void startEntry(size_t thread_id);
};

class TAIWrite : public RandomWrite
{
public: 
    TAIWrite();

    virtual void openfile(const std::string& filename) override 
    {
        using namespace tai;

        if (opencnt.fetch_add(1) > 0)
            return;
        bt.reset(new BTree<45, 3 ,4, 12>(new POSIXEngine(filename))); 
        ios[tid].reserve(2 * IO_ROUND + IO_ROUND / SYNC_RATE + 1);
    }

    virtual void closefile() override
    {
        syncop();
        ios[tid].back()->wait();
        if (opencnt.fetch_sub(1) > 1)
            return;
        bt->detach(*ctrl)->wait();
        bt.reset(nullptr);
    }

    inline void reset_cb() override
    {
        ios[tid].clear();
    }

    inline void wait_cb() override
    {
        using namespace std::chrono_literals;

        for (auto& i : ios[tid])
            i->wait(32ms);
    }

    inline void busywait_cb() override
    {
        using namespace tai;

        for (auto& i : ios[tid])
            while ((*i)() == IOCtrl::Running);
    }

    void writeop(off_t offset, char* data) override;
    void readop(off_t offset, char* data) override;
    void syncop() override;
    void cleanup() override;

private:
    std::unique_ptr<tai::BTreeBase> bt;
    std::array<tai::IOCtrlVec, MAX_THREAD_NUM> ios;

    static std::unique_ptr<tai::Controller> ctrl;
};

