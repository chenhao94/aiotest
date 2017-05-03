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

#ifdef _POSIX_VERSION
#include <errno.h>
#endif

#include "tai.hpp"

namespace tai
{
    extern std::atomic<bool> _AIO_INIT_;

    class aiocb
    {
        public:
            int             aio_fildes = 0;
            off_t           aio_offset = 0;
            volatile void*  aio_buf = nullptr;
            size_t          aio_nbytes = 0;
            int             aio_reqprio = 0;    // will ignore it
            struct sigevent aio_sigevent;       // will ignore it
            int             aio_lio_opcode;     // will ignore it

            TAI_INLINE
            explicit aiocb(int fildes = 0, off_t offset = 0, volatile void* buf = nullptr, size_t nbytes = 0) : aio_fildes(fildes), aio_offset(offset), aio_buf(buf), aio_nbytes(nbytes) {}

            TAI_INLINE
            ~aiocb()
            {
                delete io;
            }

            TAI_INLINE
            void read()
            {
                io = bts[aio_fildes].load(std::memory_order_acquire)->readsome(*ctrl, aio_offset, aio_nbytes, (char *)aio_buf);
            }

            TAI_INLINE
            void write()
            {
                io = bts[aio_fildes].load(std::memory_order_acquire)->write(*ctrl, aio_offset, aio_nbytes, (const char *)aio_buf);
            }

            TAI_INLINE
            void fsync()
            {
                io = bts[aio_fildes].load(std::memory_order_acquire)->fsync(*ctrl);
            }

            TAI_INLINE
            int status()
            {
                static int translate[] = { EINPROGRESS, EBADF, EBADF, 0 };
                return translate[(*io)()];
            }

            TAI_INLINE
            void wait()
            {
                using namespace std::chrono_literals;

                io->wait(32ms);
            }

            IOCtrl* io = nullptr;

            static std::array<std::atomic<BTreeBase*>, 65536> bts;
            static std::unique_ptr<Controller> ctrl;
    };

    TAI_INLINE
    static int aio_read(aiocb* cbp)
    {
        cbp->read();
        auto status = cbp->status();
        return status != EINPROGRESS && status;
    }

    TAI_INLINE
    static int aio_write(aiocb* cbp)
    {
        cbp->write();
        auto status = cbp->status();
        return status != EINPROGRESS && status;
    }

    TAI_INLINE
    static int aio_fsync(int op, aiocb* cbp)
    {
        cbp->fsync();
        auto status = cbp->status();
        return status != EINPROGRESS && status;
    }

    TAI_INLINE
    static int aio_error(aiocb* cbp)
    {
        return cbp->status();
    }

    TAI_INLINE
    static int aio_return(aiocb* cbp)
    {
        auto err = aio_error(cbp);
        delete cbp->io;
        cbp->io = nullptr;
        return err;
    }

    TAI_INLINE
    static int aio_wait(aiocb* cbp)
    {
        cbp->wait();
        return aio_error(cbp);
    }

    TAI_INLINE
    static bool register_fd(int fd, const std::string path)
    {
        Log::debug("Registering fd = ", fd, ".");

        if (!_AIO_INIT_.load(std::memory_order_acquire))
        {
            Log::debug("No initialized aio wrapper.");
            return false;
        }

        if (auto current = aiocb::bts[fd].load(std::memory_order_acquire))
        {
            Log::debug("Failed to register fd = ", fd, " due to the conflict with ", (size_t)current, ".");
            return false;
        }

        auto tree = new BTree<45, 3, 4, 12>(new POSIXEngine(path));
        // auto tree = new BTree<45, 3, 16>(new POSIXEngine(path, O_RDWR | O_CREAT
        //             #ifdef __linux__
        //             | O_DIRECT
        //             #endif
        //             ));
        // auto tree = new BTree<34, 4, 4, 2, 2, 2, 2, 2, 12>(new POSIXEngine(path));
        // auto tree = new BTree<34, 4, 4, 4, 4, 2, 12>(new CEngine(path));
        // auto tree = new BTree<34, 5, 2, 1, 1, 2, 7, 12>(new CEngine(path));
        // auto tree = new BTreeTrivial(new CEngine(path));
        if (tree->failed())
        {
            Log::debug("Failed to register fd = ", fd, " due to B-tree construction failure.");
            delete tree;
            return false;
        }
        aiocb::bts[fd].store(tree, std::memory_order_release);

        Log::debug("Registered fd = ", fd, " with B-tree at ", (size_t)tree, ".");
        return true;
    }

    TAI_INLINE
    static void deregister_fd(int fd)
    {
        if (!_AIO_INIT_.load(std::memory_order_acquire))
            return;
        Log::debug("Deregistering fd = ", fd, ".");

        std::unique_ptr<BTreeBase> victim(aiocb::bts[fd].load(std::memory_order_acquire));
        if (victim)
        {
            aiocb::bts[fd].store(nullptr, std::memory_order_release);
            IOCtrlVec ios;
            ios.emplace_back(victim->sync(*aiocb::ctrl))->wait();
            ios.emplace_back(victim->detach(*aiocb::ctrl))->wait();
        }

        Log::debug("Deregistered fd = ", fd, ".");
    }

    TAI_INLINE
    static void aio_init()
    {
        aiocb::ctrl.reset(new Controller(1ll << 30, 1ll << 32, -1));
        _AIO_INIT_.store(true, std::memory_order_release);
    }

    TAI_INLINE
    static void aio_end()
    {
        for (auto i = aiocb::bts.size(); i--; deregister_fd(i));
        aiocb::ctrl = nullptr;
        _AIO_INIT_.store(false, std::memory_order_release);
    }
}
