#pragma once

#include <string>

#include "sdk/events/join_method.h"
#include "sdk/schema/required.h"

namespace tracking
{
    struct SquadMember
    {
        Required<std::string> player_id;
        Required<JoinMethod> join_method;

        static void Describe(auto& visit)
        {
            visit("player_id", &SquadMember::player_id);
            visit("join_method", &SquadMember::join_method);
        }
    };
} // namespace tracking
