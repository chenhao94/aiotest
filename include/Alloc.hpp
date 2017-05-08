#pragma once

#include <cstdlib>
#include <type_traits>

#include "Decl.hpp"

#ifdef TAI_JEMALLOC
#include <jemalloc/jemalloc.h>
#else
#include <cstdlib>
#endif

namespace tai
{
    template<typename T>
    class Alloc
    {
    public:
        using value_type = T;
        using propagate_on_container_move_assignment = std::false_type;
        using is_always_equal = std::true_type;

        TAI_INLINE
        T* allocate(size_t n)
        {
            return (T*)malloc(n * sizeof(T));
        }

        TAI_INLINE
        void deallocate(T* ptr, size_t n)
        {
            free(ptr);
        }
    };
}
