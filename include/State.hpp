#pragma once

#include <string>

namespace tai
{
    enum WorkerState
    {
        Pending,
        Created,
        Pulling,
        Running,
        Unlocking,
        GC,
        Quit,
        Sync,
        Idle
    };

    const char* to_cstring(WorkerState _);
    std::string to_string(WorkerState _);
}
