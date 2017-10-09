/**
 * POSIX AIO interface wrapper
 */

#include <array>
#include <memory>
#include <atomic>

#include "tai.hpp"
#include "aio.hpp"

namespace tai
{
    std::array<std::atomic<BTreeBase*>, 65536> aiocb::bts;
    std::unique_ptr<Controller> aiocb::ctrl(nullptr);
    std::atomic<bool> _AIO_INIT_(false);
}
