#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sdk/events/mission_type.h"
#include "sdk/events/squad_member.h"
#include "sdk/schema/required.h"
#include "sdk/schema/trackable_event.h"

namespace tracking
{
    struct StartMission
    {
        static constexpr std::string_view kName = "START_MISSION";
        static constexpr std::uint32_t kVersion = 1;

        Required<MissionType> mission_type;
        Required<std::string> node;
        std::optional<std::vector<SquadMember>> squad;

        static void Describe(auto& visit)
        {
            visit("mission_type", &StartMission::mission_type);
            visit("node", &StartMission::node);
            visit("squad", &StartMission::squad);
        }
    };

    static_assert(TrackableEvent<StartMission>);
} // namespace tracking
