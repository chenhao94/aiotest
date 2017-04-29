#pragma once

/**
 * POSIX AIO interace wrapper
 */

#include <array>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <csignal>

#include "BTreeBase.hpp"
#include "IOCtrl.hpp"
#include "Controller.hpp"
#include "tai.hpp"

namespace tai
{
    class aiocb;

    void aio_init();
    void aio_end();
    int aio_read(aiocb* aiocbp);
    int aio_write(aiocb* aiocbp);
    int aio_fsync(int op, aiocb* aiocbp);
    int aio_error(aiocb* aiocbp);
    int aio_return(aiocb* aiocbp);
    int aio_wait(aiocb* aiocbp);
    bool register_fd(int fd, const std::string path);
    void deregister_fd(int fd);

    class aiocb
    {
        friend void aio_init();
        friend void aio_end();
        friend int aio_read(aiocb*);
        friend int aio_write(aiocb*);
        friend int aio_fsync(int, aiocb*);
        friend int aio_error(aiocb*);
        friend int aio_return(aiocb*);
        friend bool register_fd(int, const std::string);
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

            ~aiocb()
            {
                delete io;
            }

            static void end()
            {
                for (auto i = bts.size(); i--; deregister_fd(i));
                ctrl = nullptr;
            }

            void read()
            {
                io = bts[aio_fildes].load(std::memory_order_acquire)->readsome(*ctrl, aio_offset, aio_nbytes, (char *)aio_buf);
            }

            void write()
            {
                io = bts[aio_fildes].load(std::memory_order_acquire)->write(*ctrl, aio_offset, aio_nbytes, (const char *)aio_buf);
            }

            void fsync()
            {
                io = bts[aio_fildes].load(std::memory_order_acquire)->sync(*ctrl);
            }

            int status(); 

            void wait()
            {
                using namespace std::chrono_literals;

                io->wait(32ms);
            }

        private:
            IOCtrl* io = nullptr;

            static std::array<std::atomic<BTreeBase*>, 65536> bts;
            static std::unique_ptr<Controller> ctrl;
    };
}
