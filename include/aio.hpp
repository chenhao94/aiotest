#pragma once

/**
 * POSIX AIO interace wrapper
 */

#include <memory>
#include <atomic>
#include <signal.h>
#include "tai.hpp"

namespace tai
{
    class aiocb
    {
        public:
            static std::atomic<BTreeDefault*> bts[65536];
            static std::unique_ptr<Controller> ctrl;

            int             aio_fildes;
            off_t           aio_offset;
            volatile void  *aio_buf;
            size_t          aio_nbytes;
            int             aio_reqprio;    // will ignore it
            struct sigevent aio_sigevent;   // will ignore it
            int             aio_lio_opcode; // will ignore it

            aiocb(int fd = 0, off_t offset = 0, volatile void *buf = nullptr, size_t size = 0) : aio_fildes(fd), aio_offset(offset), aio_buf(buf), aio_nbytes(size) {}

            void read() { io.reset(bts[aio_fildes].load(std::memory_order_relaxed)->readsome(*ctrl, aio_offset, aio_nbytes, (char *)aio_buf)); }

            void write() { io.reset(bts[aio_fildes].load(std::memory_order_relaxed)->write(*ctrl, aio_offset, aio_nbytes, (const char *)aio_buf)); }

            void fsync() { io.reset(bts[aio_fildes].load(std::memory_order_relaxed)->sync(*ctrl)); }

            int status(); 

        private:
            std::unique_ptr<IOCtrl> io;
    };

    void aio_init();
    int aio_read(aiocb *aiocbp);
    int aio_write(aiocb *aiocbp);
    int aio_fsync(int op, aiocb *aiocbp);
    int aio_error(aiocb *aiocbp);
    int aio_return(aiocb *aiocbp);
    bool register_fd(int fd);
    void deregister_fd(int fd);
}
