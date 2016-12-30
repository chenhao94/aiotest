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
        Sync
    };

    std::string to_string(const WorkerState& state);
}
