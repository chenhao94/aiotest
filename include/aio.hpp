#pragma once

/**
 * POSIX AIO interace wrapper
 */

#include <array>
#include <memory>
#include <atomic>

#include <signal.h>

#include "tai.hpp"

namespace tai
{
    class aiocb;

    void aio_init();
    int aio_read(aiocb* aiocbp);
    int aio_write(aiocb* aiocbp);
    int aio_fsync(int op, aiocb* aiocbp);
    int aio_error(aiocb* aiocbp);
    int aio_return(aiocb* aiocbp);
    bool register_fd(int fd);
    void deregister_fd(int fd);

    class aiocb
    {
        friend void aio_init();
        friend int aio_read(aiocb*);
        friend int aio_write(aiocb*);
        friend int aio_fsync(int, aiocb*);
        friend int aio_error(aiocb*);
        friend int aio_return(aiocb*);
        friend bool register_fd(int);
        friend void deregister_fd(int);

        public:
            int             aio_fildes = 0;
            off_t           aio_offset = 0;
            volatile void*  aio_buf = nullptr;
            size_t          aio_nbytes = 0;
            int             aio_reqprio = 0;    // will ignore it
            struct sigevent aio_sigevent;       // will ignore it
            int             aio_lio_opcode;     // will ignore it

            explicit aiocb(int fildes = 0, off_t offset = 0, volatile void* buf = nullptr, size_t nbytes = 0) : aio_fildes(fildes), aio_offset(offset), aio_buf(buf), aio_nbytes(nbytes) {}

            void read()
            {
                io.reset(bts[aio_fildes].load(std::memory_order_consume)->readsome(*ctrl, aio_offset, aio_nbytes, (char *)aio_buf));
            }

            void write()
            {
                io.reset(bts[aio_fildes].load(std::memory_order_consume)->write(*ctrl, aio_offset, aio_nbytes, (const char *)aio_buf));
            }

            void fsync()
            {
                io.reset(bts[aio_fildes].load(std::memory_order_consume)->sync(*ctrl));
            }

            int status(); 

        private:
            std::unique_ptr<IOCtrl> io;

            static std::array<std::atomic<BTreeDefault*>, 65536> bts;
            static std::unique_ptr<Controller> ctrl;
    };
}
