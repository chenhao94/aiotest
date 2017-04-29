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
        #ifdef _POSIX_VERSION
        fd = open(filename.c_str(), openflags);
        #else
        std::cerr << "RandomWrite::openfile() needs POSIX support." << std::endl;
        #endif
    }

    virtual void closefile()
    {
        #ifdef _POSIX_VERSION
        close(fd);
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

    static std::unique_ptr<RandomWrite> getInstance(int testType);
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

class FstreamWrite : public BlockingWrite
{
public: 
    FstreamWrite() {}

    void writeop(off_t offset, char* data) override
    {
        file.seekp(offset).write(data, WRITE_SIZE);
    }

    void readop(off_t offset, char* data) override
    {
        file.seekg(offset).read(data, READ_SIZE);
    }

    void syncop() override
    {
        file.flush();
        BlockingWrite::syncop();
    }

    virtual void openfile(const std::string& filename) override
    {
        RandomWrite::openfile(filename);
        if (file.is_open())
            file.close();
        file.open(filename, std::ios::binary | std::ios::in | std::ios::out);
    }

    static void startEntry(size_t thread_id);

private:
    std::fstream file;
};

class AIOWrite : public RandomWrite
{
public:
    AIOWrite()
    {
        #ifdef _POSIX_VERSION
        cbs.reserve(2 * IO_ROUND + IO_ROUND / SYNC_RATE + 1);
        #else
        std::cerr << "Warning: POSIX AIO needs POSIX support." << std::endl;
        #endif
    }

    #ifdef _POSIX_VERSION
    std::vector<aiocb> cbs;
    #endif

    inline void reset_cb() override
    {
        #ifdef _POSIX_VERSION
        cbs.clear();
        #else
        std::cerr << "Warning: POSIX AIO needs POSIX support." << std::endl;
        #endif
    }

    inline void wait_cb() override
    {
        using namespace std::chrono_literals;

        #ifdef _POSIX_VERSION
        for (; aio_error(&(cbs.back())) == EINPROGRESS; std::this_thread::sleep_for(1ms));
        #else
        std::cerr << "Warning: POSIX AIO needs POSIX support." << std::endl;
        #endif
    }

    inline void busywait_cb() override
    {
        #ifdef _POSIX_VERSION
        while (aio_error(&(cbs.back())) == EINPROGRESS);
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
        cbs.reserve(2 * IO_ROUND + IO_ROUND / SYNC_RATE + 1);
        openflags |= O_DIRECT;
        #else
        std::cerr << "Warning: LibAIO is not supported on non-Linux system." << std::endl;
        #endif
    }

    #ifdef __linux__
    std::vector<iocb*> cbs;
    io_event events[65536];
    static io_context_t io_cxt;
    #endif

    void reset_cb() override;
    void wait_cb() override;
    void busywait_cb() override { wait_cb(); } // no way to busywait
    void writeop(off_t offset, char* data) override;
    void readop(off_t offset, char* data) override;
    void syncop() override;
    static void startEntry(size_t thread_id);

    int cnt = 0;
};

class TAIAIOWrite : public RandomWrite
{
public: 
    TAIAIOWrite()
    {
        cbs.reserve(2 * IO_ROUND + IO_ROUND / SYNC_RATE + 1);
    }

    std::vector<tai::aiocb> cbs;

    inline void reset_cb() override
    {
        cbs.clear();
    }

    inline void wait_cb() override
    {
        if (tai::aio_fsync(O_SYNC, &cbs.emplace_back(fd)))
        {
            std::cerr << "Error " << errno << ": " << strerror(errno) << " at tai::aio_fsync." << std::endl;
            exit(-1);
        }
        tai::aio_wait(&cbs.back());
    }

    inline void busywait_cb() override
    {
        if (tai::aio_fsync(O_SYNC, &cbs.emplace_back(fd)))
        {
            std::cerr << "Error " << errno << ": " << strerror(errno) << " at tai::aio_fsync." << std::endl;
            exit(-1);
        }
        while (tai::aio_error(&cbs.back()) == EINPROGRESS);
    }

    void cleanup() override;
    void writeop(off_t offset, char* data) override;
    void readop(off_t offset, char* data) override;
    void syncop() override;

    virtual void openfile(const std::string& filename) override
    {
        RandomWrite::openfile(filename);
        tai::register_fd(fd, filename);
    }

    virtual void closefile() override
    {
        tai::deregister_fd(fd);
    }

    static void startEntry(size_t thread_id);
};

class TAIWrite : public RandomWrite
{
public: 
    TAIWrite();

    virtual void openfile(const std::string& filename) override 
    {
        bt.reset(new tai::BTreeDefault(new tai::POSIXEngine(filename))); 
        ios.reserve(2 * IO_ROUND + IO_ROUND / SYNC_RATE + 1);
    }

    virtual void closefile() override
    {
        auto io = bt->detach(*ctrl);
        bt.reset(nullptr);
        io->wait();
    }

    inline void reset_cb() override
    {
        ios.clear();
    }

    inline void wait_cb() override
    {
        ios.back()->wait();
    }

    inline void busywait_cb() override
    {
        while ((*ios.back())() == tai::IOCtrl::Running);
    }

    void writeop(off_t offset, char* data) override;
    void readop(off_t offset, char* data) override;
    void syncop() override;

private:
    std::unique_ptr<tai::BTreeBase> bt;
    tai::IOCtrlVec ios;

    static std::unique_ptr<tai::Controller> ctrl;
    static std::atomic<bool> ctrlFlag;
    static std::atomic<bool> ctrlConstructedFlag;
};

