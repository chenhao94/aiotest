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

inline auto randgen(size_t align = 0)
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

    virtual void writeop(off_t offset, char* data) = 0;
    virtual void readop(off_t offset, char* data) = 0;
    virtual void syncop() = 0;
    virtual void reset_cb() {}
    virtual void wait_cb() {}
    virtual void busywait_cb() {}
    virtual void cleanup() {}
};

class BlockingWrite : public RandomWrite
{
public: 

    BlockingWrite() {}

    void writeop(off_t offset, char* data) override;
    void readop(off_t offset, char* data) override;
    void syncop() override;
    static void startEntry(size_t thread_id, int flags = 0);
};

class AIOWrite : public RandomWrite
{
public:

    AIOWrite() { cbs.reserve(2 * IO_ROUND + IO_ROUND / SYNC_RATE + 1); }

    std::vector<aiocb> cbs;

    inline void reset_cb() override { cbs.clear(); }

    inline void wait_cb() override
    {
        using namespace std::chrono_literals;
        while (aio_error(&(cbs.back())) == EINPROGRESS)
            std::this_thread::sleep_for(1ms);
    }

    inline void busywait_cb() override
    {
        while (aio_error(&(cbs.back())) == EINPROGRESS);
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
        cbs.reserve(2 * IO_ROUND + IO_ROUND / SYNC_RATE + 1); openflags |= O_DIRECT;
        #else
        std::cerr << "Warning: LibAIO is not supported on non-Linux system." << std::endl;
        #endif
    }

    #ifdef __linux__
    std::vector<iocb*> cbs;
    #endif

    void reset_cb() override;
    void wait_cb() override;
    void busywait_cb() override { wait_cb(); } // no way to busywait
    void writeop(off_t offset, char* data) override;
    void readop(off_t offset, char* data) override;
    void syncop() override;
    static void startEntry(size_t thread_id);

    int cnt = 0;

    #ifdef __linux__
    io_event events[65536];
    static io_context_t io_cxt;
    #endif
};

class TAIWrite : public RandomWrite
{
public: 

    TAIWrite() { cbs.reserve(2 * IO_ROUND + IO_ROUND / SYNC_RATE + 1); }

    std::vector<tai::aiocb> cbs;

    inline void reset_cb() override { cbs.clear(); }

    inline void wait_cb() override
    {
        using namespace std::chrono_literals;
        while (tai::aio_error(&(cbs.back())) == EINPROGRESS)
            std::this_thread::sleep_for(1ms);
    }

    inline void busywait_cb() override
    {
        while (tai::aio_error(&(cbs.back())) == EINPROGRESS);
    }

    void cleanup() override;
    void writeop(off_t offset, char* data) override;
    void readop(off_t offset, char* data) override;
    void syncop() override;
    static void startEntry(size_t thread_id);
};
