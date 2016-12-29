#pragma once

namespace tai
{
    enum WorkerState
    {
        Pending,
        Created,
        Pulling,
        Ready,
        Running,
        Unlocking,
        GC,
        Closing,
        Sync,
        Idle
    };
}
