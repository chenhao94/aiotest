#pragma once

#include <iostream>
#include <string>
#include <iomanip>
#include <atomic>

namespace tai::Log
{
    template<typename T>
    class to_string
    {
    public:
        static auto get(T elem)
        {
            return std::to_string(elem);
        }
    };

    template<>
    class to_string<const std::string>
    {
    public:
        static auto get(const std::string& elem)
        {
            return elem;
        }
    };

    template<>
    class to_string<char*>
    {
    public:
        static auto get(char* elem)
        {
            return std::string(elem);
        }
    };

    template<>
    class to_string<char const *>
    {
    public:
        static auto get(char const * elem)
        {
            return std::string(elem);
        }
    };

    template<>
    class to_string<char* const>
    {
    public:
        static auto get(char* const elem)
        {
            return std::string(elem);
        }
    };

    template<>
    class to_string<char const * const>
    {
    public:
        static auto get(char const * const elem)
        {
            return std::string(elem);
        }
    };

    template<typename First>
    auto concat(First first)
    {
        return to_string<const First>::get(first);
    }

    template<typename First, typename... Rest>
    auto concat(First first, Rest... rest)
    {
        return to_string<const First>::get(first) + concat(rest...);
    }

    template<typename... T>
    void debug(T&&... elem)
    {
        std::cerr << concat(elem...) + "\n" << std::flush;
    }

    template<typename... T>
    void log(T&&... elem)
    {
        std::cerr << concat(elem...) + "\n" << std::flush;
    }

    void debug_counter_add(std::atomic<ssize_t>& counter, ssize_t num);

    extern std::atomic<ssize_t> barrier_time;
    extern std::atomic<ssize_t> steal_time;
    extern std::atomic<ssize_t> run_time;
}
