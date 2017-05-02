#pragma once

#include <string>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <vector>
#include <atomic>
#include <memory>
#include <limits>

#if defined(__unix__) || defined(__MACH__)
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

#include "Decl.hpp"
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

        virtual std::string str() = 0;

        TAI_INLINE
        virtual bool read(char* dat, size_t pos, size_t len)
        {
            Log::log("Warning: ", str()," does not support read(dat, pos, len)");
            return false;
        }

        TAI_INLINE
        virtual bool write(const char* dat, size_t pos, size_t len)
        {
            Log::log("Warning: ", str()," does not support write(dat, pos, len)");
            return false;
        }

        TAI_INLINE
        virtual bool fsync()
        {
            Log::log("Warning: ", str()," does not support fsync()");
            return false;
        }
    };

    class STLEngine : public IOEngine, public TLTable<std::fstream>
    {
        std::string path;

        std::fstream& get();

    public:
        TAI_INLINE
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

        TAI_INLINE
        virtual std::string str() override
        {
            return "[STLEngine: \"" + path + "\"]";
        }

        TAI_INLINE
        virtual bool read(char* dat, size_t pos, size_t len) override
        {
            return bool(get().seekg(pos).read(dat, len));
        }

        TAI_INLINE
        virtual bool write(const char* dat, size_t pos, size_t len) override
        {
            return bool(get().seekp(pos).write(dat, len));
        }
    };

    class CEngine : public IOEngine, public TLTable<FILE*>
    {
        std::string path;

        FILE* get();

    public:
        TAI_INLINE
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

        TAI_INLINE
        ~CEngine()
        {
            while (mtx.test_and_set(std::memory_order_acquire));
            for (auto& i : files)
                if (*i)
                    fclose(*i);
            mtx.clear(std::memory_order_release);
        }

        TAI_INLINE
        virtual std::string str() override
        {
            return "[CEngine: \"" + path + "\"]";
        }

        TAI_INLINE
        virtual bool read(char* dat, size_t pos, size_t len) override
        {
            auto file = get();
            return !fseek(file, pos, SEEK_SET) && fread(dat, len, 1, file) == 1;
        }

        TAI_INLINE
        virtual bool write(const char* dat, size_t pos, size_t len) override
        {
            auto file = get();
            return !fseek(file, pos, SEEK_SET) && fwrite(dat, len, 1, file) == 1 && !fflush(file);
        }
    };

#ifdef _POSIX_VERSION
    class POSIXEngine : public IOEngine
    {
        std::string path;
        int fd = -1;

        TAI_INLINE
        void loadSize()
        {
            if (~fd)
                size = lseek(fd, 0, SEEK_END);
            else
                Log::log("[Warning] Cannot open ", str());
        }

    public:
        TAI_INLINE
        explicit POSIXEngine(const std::string& path) : path(path), fd(open(path.c_str(), O_RDWR | O_CREAT))
        {
            if (!~fd)
                fd = open(path.c_str(), O_RDONLY | O_CREAT);

            loadSize();
        }

        TAI_INLINE
        explicit POSIXEngine(const std::string& path, int flags) : path(path), fd(open(path.c_str(), flags))
        {
            loadSize();
        }

        TAI_INLINE
        explicit POSIXEngine(int fd) : fd(fd)
        {
            loadSize();
        }

        TAI_INLINE
        ~POSIXEngine()
        {
            if (~fd)
                close(fd);
        }

        TAI_INLINE
        virtual std::string str() override
        {
            return "[POSIXEngine(fd = " + std::to_string(fd) + "): \"" + path + "\"]";
        }

        TAI_INLINE
        virtual bool read(char* dat, size_t pos, size_t len) override
        {
            return pread(fd, dat, len, pos) == len;
        }

        TAI_INLINE
        virtual bool write(const char* dat, size_t pos, size_t len) override
        {
            return pwrite(fd, dat, len, pos) == len;
        }

        TAI_INLINE
        virtual bool fsync() override
        {
            return !::fsync(fd);
        }
    };
#endif

    class NullEngine : public IOEngine
    {
    public:
        template<typename... Args>
        TAI_INLINE
        explicit NullEngine(Args... args)
        {
            size = std::numeric_limits<decltype(size)>::max();
        }

        TAI_INLINE
        virtual std::string str() override
        {
            return "[NullEngine]";
        }

        TAI_INLINE
        virtual bool read(char* dat, size_t pos, size_t len) override
        {
            return true;
        }

        TAI_INLINE
        virtual bool write(const char* dat, size_t pos, size_t len) override
        {
            return true;
        }
    };
}
