#include "IOEngine.hpp"
#include "Worker.hpp"

namespace tai
{
    std::fstream& STLEngine::get()
    {
        auto& file = Worker::getTL(files, mtx);
        if (file && file.is_open())
            file.close();
        if (!file.is_open())
            file.open(path, std::ios_base::in | std::ios_base::out | std::ios_base::binary);
        if (!file.is_open())
            file.open(path, std::ios_base::in | std::ios_base::binary);
        return file;
    }
}
