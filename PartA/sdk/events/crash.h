#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "sdk/schema/required.h"
#include "sdk/schema/trackable_event.h"

namespace tracking
{
    // Sent on the launch after a crash.
    struct Crash
    {
        static constexpr std::string_view kName = "CRASH";
        static constexpr std::uint32_t kVersion = 1;

        Required<std::string> crashed_session_id;

        static void Describe(auto& visit)
        {
            visit("crashed_session_id", &Crash::crashed_session_id);
        }
    };

    static_assert(TrackableEvent<Crash>);
} // namespace tracking
