#include "State.hpp"

namespace tai
{
    const char* to_cstring(WorkerState _)
    {
        switch (_)
        {
        case Pending:
            return "Pending";
        case Created:
            return "Created";
        case Pulling:
            return "Pulling";
        case Running:
            return "Running";
        case Unlocking:
            return "Unlocking";
        case Cleanup:
            return "Cleanup";
        case Idle:
            return "Idle";
        case GC:
            return "GC";
        case Quit:
            return "Quit";
        case Sync:
            return "Sync";
        }
        return "Unknown";
    }

    std::string to_string(WorkerState _)
    {
        return to_cstring(_);
    }
}
