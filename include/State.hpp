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
        Sync
    };

    const char* to_cstring(WorkerState state);
    std::string to_string(WorkerState state);
}
