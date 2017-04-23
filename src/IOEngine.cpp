#include <cstdio>
#include <fstream>
#include <iomanip>

#include "IOEngine.hpp"
#include "Worker.hpp"

namespace tai
{
    std::fstream& STLEngine::get()
    {
        auto& file = Worker::getTL(files, mtx);
        if (!file.is_open() || !file)
        {
            if (file.is_open())
                file.close();
            file.open(path, std::ios_base::in | std::ios_base::out | std::ios_base::binary);
            if (file.is_open())
                ;//file.rdbuf()->pubsetbuf(nullptr, 0);
            else
            {
                file.open(path, std::ios_base::in | std::ios_base::binary);
                if (file.is_open())
                    ;//file.rdbuf()->pubsetbuf(nullptr, 0);
            }
        }
        return file;
    }

    FILE* CEngine::get()
    {
        auto& file = Worker::getTL(files, mtx, nullptr);
        if (file)
        {
            if (!ferror(file) || freopen(nullptr, "r+b", file) || freopen(nullptr, "rb", file) || freopen(nullptr, "w+b", file))
                return file;
            fclose(file);
        }
        if ((file = fopen(path.c_str(), "r+b")) || (file = fopen(path.c_str(), "rb")))
            return file;
        return file = fopen(path.c_str(), "w+b");
    }
}
