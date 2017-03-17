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
        Cleanup,
        Idle,
        GC,
        Flushing,
        Quit,
        Sync
    };

    const char* to_cstring(WorkerState _);
    std::string to_string(WorkerState _);
}
