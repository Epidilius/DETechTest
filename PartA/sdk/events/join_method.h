#pragma once

#include <string_view>

namespace tracking
{
    enum class JoinMethod
    {
        Party,
        Matchmaking
    };

    constexpr std::string_view ToString(JoinMethod join_method)
    {
        switch (join_method)
        {
            case JoinMethod::Party:
            {
                return "PARTY";
            }
            case JoinMethod::Matchmaking:
            {
                return "MATCHMAKING";
            }
        }

        return "INVALID";
    }
} // namespace tracking
