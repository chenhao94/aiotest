#include "IOCtrl.hpp"

namespace tai
{
    const char* to_cstring(IOCtrl::State state)
    {
        switch (state)
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

    std::string to_string(IOCtrl::State state)
    {
        return to_cstring(state);
    }
}
