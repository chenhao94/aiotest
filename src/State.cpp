#include "State.hpp"

namespace tai
{
    const char* to_cstring(WorkerState state)
    {
        switch (state)
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
        case GC:
            return "GC";
        case Sync:
            return "Sync";
        }
        return "Unknown";
    }

    std::string to_string(WorkerState state)
    {
        return to_cstring(state);
    }
}
