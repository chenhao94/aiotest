#pragma once

#include <string>
#include <fstream>
#include <iomanip>
#include <vector>
#include <atomic>
#include <memory>

#include "Log.hpp"

namespace tai
{
    class IOEngine
    {
    public:
        std::atomic_flag mtx = ATOMIC_FLAG_INIT;
        size_t size = 0;

        virtual bool read(char* dat, size_t pos, size_t len) = 0;
        virtual bool write(const char* dat, size_t pos, size_t len) = 0;

        virtual std::string str() = 0;
    };

    class STLEngine : public IOEngine
    {
        std::string path;
        std::vector<std::unique_ptr<std::fstream>> files;

        std::fstream& get();

    public:
        STLEngine(const std::string& path) : path(path)
        {
            std::fstream file(path, std::ios_base::in | std::ios_base::binary);
            if (file.is_open())
                size = file.seekg(0, std::ios_base::end).tellg();
            else
                file.open(path, std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);

            if (!file.is_open())
                Log::log("[Warning] Cannot open \"", str(), "\"");
        }

        virtual bool read(char* dat, size_t pos, size_t len)
        {
            return bool(get().seekg(pos).read(dat, len));
        }

        virtual bool write(const char* dat, size_t pos, size_t len)
        {
            return bool(get().seekp(pos).write(dat, len).flush());
        }

        virtual std::string str()
        {
            return "[STLEngine: \"" + path + "\"]";
        }
    };
}
