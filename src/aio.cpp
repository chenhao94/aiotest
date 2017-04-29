/**
 * POSIX AIO interface wrapper
 */

#include "aio.hpp"

#include <array>
#include <memory>
#include <atomic>

#include <errno.h>

#include "BTree.hpp"
#include "IOEngine.hpp"
#include "IOCtrl.hpp"
#include "Controller.hpp"
#include "tai.hpp"

namespace tai
{
    std::array<std::atomic<BTreeBase*>, 65536> aiocb::bts;
    std::unique_ptr<Controller> aiocb::ctrl(nullptr);
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
        aiocb::ctrl.reset(new Controller(1ll << 30, 1ll << 32));
        _AIO_INIT_.store(true, std::memory_order_seq_cst);
    }

    void aio_end()
    {
        aiocb::end();
        _AIO_INIT_.store(false, std::memory_order_seq_cst);
    }

    int aio_read(aiocb* cbp)
    {
        cbp->read();
        auto status = cbp->status();
        return status != EINPROGRESS && status;
    }

    int aio_write(aiocb* cbp)
    {
        cbp->write();
        auto status = cbp->status();
        return status != EINPROGRESS && status;
    }

    int aio_fsync(int op, aiocb* cbp)
    {
        // ignore op
        cbp->fsync();
        auto status = cbp->status();
        return status != EINPROGRESS && status;
    }

    int aio_error(aiocb* cbp)
    {
        return cbp->status();
    }

    int aio_return(aiocb* cbp)
    {
        auto err = aio_error(cbp);
        delete cbp->io;
        cbp->io = nullptr;
        return err;
    }

    int aio_wait(aiocb* cbp)
    {
        cbp->wait();
        return aio_error(cbp);
    }

    bool register_fd(int fd, const std::string path)
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

    void deregister_fd(int fd)
    {
        if (!_AIO_INIT_.load(std::memory_order_acquire))
            return;
        Log::debug("Deregistering fd = ", fd, ".");

        std::unique_ptr<BTreeBase> victim(aiocb::bts[fd].load(std::memory_order_acquire));
        if (victim)
        {
            aiocb::bts[fd].store(nullptr, std::memory_order_release);
            IOCtrlVec ios;
            ios.emplace_back(victim->sync(*aiocb::ctrl));
            ios.emplace_back(victim->detach(*aiocb::ctrl))->wait();
        }

        Log::debug("Deregistered fd = ", fd, ".");
    }
}
