#pragma once

#include <string>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <vector>
#include <atomic>
#include <memory>
#include <limits>

#ifdef __unix__
#include <unistd.h>
#endif

#ifdef _POSIX_VERSION
#include <fcntl.h>
#include <errno.h>
#endif

#ifdef __linux__
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include "Log.hpp"

namespace tai
{
    template<typename T>
    class TLTable
    {
    public:
        std::atomic_flag mtx = ATOMIC_FLAG_INIT;
        std::vector<std::unique_ptr<T>> files;
    };

    class IOEngine
    {
    public:
        size_t size = 0;

        virtual bool read(char* dat, size_t pos, size_t len) = 0;
        virtual bool write(const char* dat, size_t pos, size_t len) = 0;

        virtual std::string str() = 0;
    };

    class STLEngine : public IOEngine, public TLTable<std::fstream>
    {
        std::string path;

        std::fstream& get();

    public:
        explicit STLEngine(const std::string& path) : path(path)
        {
            std::fstream file(path, std::ios_base::in | std::ios_base::binary);
            if (file.is_open())
                size = file.seekg(0, std::ios_base::end).tellg();
            else
                file.open(path, std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);

            if (!file.is_open())
                Log::log("[Warning] Cannot open ", str());

            file.rdbuf()->pubsetbuf(nullptr, 0);
        }

        virtual bool read(char* dat, size_t pos, size_t len) override
        {
            return bool(get().seekg(pos).read(dat, len));
        }

        virtual bool write(const char* dat, size_t pos, size_t len) override
        {
            return bool(get().seekp(pos).write(dat, len));
        }

        virtual std::string str() override
        {
            return "[STLEngine: \"" + path + "\"]";
        }
    };

    class CEngine : public IOEngine, public TLTable<FILE*>
    {
        std::string path;

        FILE* get();

    public:
        explicit CEngine(const std::string& path) : path(path)
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
                Log::log("[Warning] Cannot open ", str());

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

        virtual bool read(char* dat, size_t pos, size_t len) override
        {
            auto file = get();
            return !fseek(file, pos, SEEK_SET) && fread(dat, len, 1, file) == 1;
        }

        virtual bool write(const char* dat, size_t pos, size_t len) override
        {
            auto file = get();
            return !fseek(file, pos, SEEK_SET) && fwrite(dat, len, 1, file) == 1 && !fflush(file);
        }

        virtual std::string str() override
        {
            return "[CEngine: \"" + path + "\"]";
        }
    };

#ifdef _POSIX_VERSION
    class POSIXEngine : public IOEngine
    {
        std::string path;
        int fd = -1;

        void loadSize()
        {
            if (~fd)
                size = lseek(fd, 0, SEEK_END);
            else
                Log::log("[Warning] Cannot open ", str());
        }

    public:
        explicit POSIXEngine(const std::string& path) : path(path), fd(open(path.c_str(), O_RDWR | O_CREAT))
        {
            if (!~fd)
                fd = open(path.c_str(), O_RDONLY | O_CREAT);

            loadSize();
        }

        explicit POSIXEngine(const std::string& path, int flags) : path(path), fd(open(path.c_str(), flags))
        {
            loadSize();
        }

        explicit POSIXEngine(int fd) : fd(fd)
        {
            loadSize();
        }

        ~POSIXEngine()
        {
            if (~fd)
                close(fd);
        }

        virtual bool read(char* dat, size_t pos, size_t len) override
        {
            return pread(fd, dat, len, pos) == len;
        }

        virtual bool write(const char* dat, size_t pos, size_t len) override
        {
            return pwrite(fd, dat, len, pos) == len;
        }

        virtual std::string str() override
        {
            return "[POSIXEngine(fd = " + std::to_string(fd) + "): \"" + path + "\"]";
        }
    };
#endif

    class NullEngine : public IOEngine
    {
    public:
        template<typename... Args>
        explicit NullEngine(Args... args)
        {
            size = std::numeric_limits<decltype(size)>::max();
        }

        virtual bool read(char* dat, size_t pos, size_t len) override
        {
            return true;
        }

        virtual bool write(const char* dat, size_t pos, size_t len) override
        {
            return true;
        }

        virtual std::string str() override
        {
            return "[NullEngine]";
        }
    };
}
