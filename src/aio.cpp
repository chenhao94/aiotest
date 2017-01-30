/**
 * POSIX AIO interface wrapper
 */

#include "aio.hpp"

#include <array>
#include <memory>
#include <atomic>

#include <errno.h>

#include "tai.hpp"

namespace tai
{
    std::array<std::atomic<BTreeDefault*>, 65536> aiocb::bts;
    std::unique_ptr<Controller> aiocb::ctrl;

    int aiocb::status()
    {
        auto status =  io->state.load(std::memory_order_relaxed);
        if (status == IOCtrl::Rejected)
            return errno = EAGAIN;
        else if (status == IOCtrl::Failed)
            return errno = EBADF;
        else if (status == IOCtrl::Running)
            return EINPROGRESS;
        else
            return 0;
    }

    void aio_init()
    {
        aiocb::ctrl.reset(new Controller(1 << 28, 1 << 30));
    }

    int aio_read(aiocb* aiocbp)
    {
        aiocbp->read();
        auto status = aiocbp->status();
        return status != EINPROGRESS && status;
    }

    int aio_write(aiocb* aiocbp)
    {
        aiocbp->write();
        auto status = aiocbp->status();
        return status != EINPROGRESS && status;
    }

    int aio_fsync(int op, aiocb* aiocbp)
    {
        // ignore op
        aiocbp->fsync();
        auto status = aiocbp->status();
        return status != EINPROGRESS && status;
    }

    int aio_error(aiocb* aiocbp)
    {
        return aiocbp->status();
    }

    int aio_return(aiocb* aiocbp)
    {
        return aio_error(aiocbp);
    }

    bool register_fd(int fd)
    {
        if (aiocb::bts[fd].load(std::memory_order_consume))
            return false;
        auto tree = new BTreeDefault("/proc/self/fd/" + std::to_string(fd)) ;
        if (tree->conf.failed.load(std::memory_order_relaxed))
        {
            delete tree;
            return false;
        }
        aiocb::bts[fd].store(tree, std::memory_order_release);
        return true;
    }

    void deregister_fd(int fd)
    {
        delete aiocb::bts[fd].load(std::memory_order_consume);
        aiocb::bts[fd].store(nullptr, std::memory_order_release);
    }
}
