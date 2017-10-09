#pragma once

#include <string>

#include "Decl.hpp"

namespace tai
{
    enum WorkerState
    {
        Pending,
        Created,
        Pulling,
        Running,
        Unlocking,
        Cleanup,
        Idle,
        GC,
        Flushing,
        Quit,
        Sync
    };

    TAI_INLINE
    static const char* to_cstring(WorkerState _)
    {
        static const char* str[] = {
            "Pending",
            "Created",
            "Pulling",
            "Running",
            "Unlocking",
            "Cleanup",
            "Idle",
            "GC",
            "Flushing",
            "Quit",
            "Sync"
        };

        return str[_];
    }

    TAI_INLINE
    static std::string to_string(WorkerState _)
    {
        return to_cstring(_);
    }
}
