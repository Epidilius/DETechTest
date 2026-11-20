#pragma once

#include <string_view>

namespace tracking
{
    enum class MissionType
    {
        Defense,
        Survival,
        Capture
    };

    constexpr std::string_view ToString(MissionType mission_type)
    {
        switch (mission_type)
        {
            case MissionType::Defense:
            {
                return "DEFENSE";
            }
            case MissionType::Survival:
            {
                return "SURVIVAL";
            }
            case MissionType::Capture:
            {
                return "CAPTURE";
            }
        }

        return "INVALID";
    }
} // namespace tracking
