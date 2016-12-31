#pragma once

#include <iostream>
#include <string>
#include <iomanip>

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
        static auto get(const char* const elem)
        {
            return elem;
        }
    };

    template<>
    class to_string<const char* const>
    {
    public:
        static auto get(const char* const elem)
        {
            return std::string(elem);
        }
    };

    template<typename First>
    auto concat(First first)
    {
        return to_string<const First const>::get(first);
    }

    template<typename First, typename... Rest>
    auto concat(First first, Rest... rest)
    {
        return to_string<const First const>::get(first) + concat(rest...);
    }

    template<typename... T>
    void log(T&&... elem)
    {
        std::cerr << concat(elem...) + "\n" << std::flush;
    }
}
