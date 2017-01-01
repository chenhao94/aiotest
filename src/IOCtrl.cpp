#include "IOCtrl.hpp"

namespace tai
{
    const char* to_cstring(IOCtrl::State _)
    {
        switch (_)
        {
        case IOCtrl::Running:
            return "Running";
        case IOCtrl::Failed:
            return "Failed";
        case IOCtrl::Rejected:
            return "Rejected";
        case IOCtrl::Done:
            return "Done";
        }
        return "Unknown";
    }

    const char* to_cstring(IOCtrl::Method _)
    {
        switch (_)
        {
        case IOCtrl::Lock:
            return "Lock";
        case IOCtrl::Timing:
            return "Timing";
        }
        return "Unknown";
    }

    std::string to_string(IOCtrl::State _)
    {
        return to_cstring(_);
    }

    std::string to_string(IOCtrl::Method _)
    {
        return to_cstring(_);
    }
}
