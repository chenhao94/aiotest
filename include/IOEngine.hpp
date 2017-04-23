#pragma once

#include <string>
#include <cstdio>
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

            file.rdbuf()->pubsetbuf(nullptr, 0);
        }

        virtual bool read(char* dat, size_t pos, size_t len)
        {
            return bool(get().seekg(pos).read(dat, len));
        }

        virtual bool write(const char* dat, size_t pos, size_t len)
        {
            return bool(get().seekp(pos).write(dat, len));
        }

        virtual std::string str()
        {
            return "[STLEngine: \"" + path + "\"]";
        }
    };

    class CEngine : public IOEngine
    {
        std::string path;
        std::vector<std::unique_ptr<FILE*>> files;

        FILE* get();

    public:
        CEngine(const std::string& path) : path(path)
        {
            auto file = fopen(path.c_str(), "rb");
            if (ferror(file))
                file = fopen(path.c_str(), "wb");
            else
            {
                fseek(file, 0, SEEK_END);
                size = ftell(file);
            }

            if (ferror(file))
                Log::log("[Warning] Cannot open \"", str(), "\"");

            fclose(file);
        }

        ~CEngine()
        {
            while (mtx.test_and_set(std::memory_order_acquire));
            for (auto& i : files)
                if (*i)
                    fclose(*i);
            mtx.clear(std::memory_order_release);
        }

        virtual bool read(char* dat, size_t pos, size_t len)
        {
            auto file = get();
            return !fseek(file, pos, SEEK_SET) && fread(dat, len, 1, file) == 1;
        }

        virtual bool write(const char* dat, size_t pos, size_t len)
        {
            auto file = get();
            return !fseek(file, pos, SEEK_SET) && fwrite(dat, len, 1, file) == 1;
        }

        virtual std::string str()
        {
            return "[CEngine: \"" + path + "\"]";
        }
    };
}
