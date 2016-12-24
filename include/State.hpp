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
        Done,
        Idle
    };
}
