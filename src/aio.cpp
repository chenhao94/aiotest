/**
 * POSIX AIO interface wrapper
 */

#include <errno.h>
#include "aio.hpp"

namespace tai
{
    std::atomic<BTreeDefault*> aiocb::bts[65536];
    std::unique_ptr<Controller> ctrl;

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

    bool register_fd(int fd) { aiocb::bts[fd].store(new BTreeDefault("/proc/self/fd/" + std::to_string(fd)), std::memory_order_release); }

    void deregister_fd(int fd) { aiocb::bts[fd].store(nullptr, std::memory_order_release); }

    void aio_init()
    {
        aiocb::ctrl.reset(new Controller(1 << 28, 1 << 30));
    }

    int aio_read(aiocb *aiocbp)
    {
        aiocbp->read();
        auto status = aiocbp->status();
        return (status != EINPROGRESS) && status;
    }

    int aio_write(aiocb *aiocbp)
    {
        aiocbp->write();
        auto status = aiocbp->status();
        return (status != EINPROGRESS) && status;
    }

    int aio_fsync(int op, aiocb *aiocbp)
    {
        // ignore op
        aiocbp->fsync();
        auto status = aiocbp->status();
        return (status != EINPROGRESS) && status;
    }

    int aio_error(aiocb *aiocbp) { return aiocbp->status(); }

    int aio_return(aiocb *aiocbp) { return aio_error(aiocbp); }
}
