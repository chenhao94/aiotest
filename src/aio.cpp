/**
 * POSIX AIO interface wrapper
 */

#include "aio.hpp"

#include <array>
#include <memory>
#include <atomic>

#include <errno.h>

#include "BTree.hpp"
#include "IOCtrl.hpp"
#include "Controller.hpp"
#include "tai.hpp"

namespace tai
{
    std::array<std::atomic<BTreeBase*>, 65536> aiocb::bts;
    std::unique_ptr<Controller> aiocb::ctrl;
    std::atomic<bool> _AIO_INIT_(false);

    int aiocb::status()
    {
        switch ((*io)())
        {
        case IOCtrl::Running:
            return EINPROGRESS;
        case IOCtrl::Rejected:
            return errno = EAGAIN;
        case IOCtrl::Failed:
            return errno = EBADF;
        case IOCtrl::Done:
            return 0;
        }
        return 0;
    }

    void aio_init()
    {
        aiocb::init();
        _AIO_INIT_.store(true, std::memory_order_seq_cst);
    }

    void aio_end()
    {
        aiocb::end();
        _AIO_INIT_.store(false, std::memory_order_seq_cst);
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

    bool register_fd(int fd, const std::string path)
    {
        Log::debug("Registering fd = ", fd, ".");
        if (!_AIO_INIT_.load(std::memory_order_consume))
        {
            Log::debug("Not initialized aio wrapper.");
            return false;
        }
        if (auto current = aiocb::bts[fd].load(std::memory_order_consume))
        {
            Log::debug("Failed to register fd = ", fd, " due to the conflict with ", (size_t)current, ".");
            return false;
        }

        auto tree = new BTree<34, 4, 4, 4, 4, 2, 12>(path);
        // auto tree = new BTree<34, 5, 2, 1, 1, 2, 7, 12>(path);
        // auto tree = new BTreeTrivial(path);
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

    void deregister_fd(int fd)
    {
        if (!_AIO_INIT_.load(std::memory_order_consume))
            return;
        Log::debug("Deregistering fd = ", fd, ".");

        auto victim = aiocb::bts[fd].load(std::memory_order_consume);
        delete victim;
        aiocb::bts[fd].store(nullptr, std::memory_order_release);

        Log::debug("Deregistered fd = ", fd, " with victim ", (size_t)victim, ".");
    }
}
