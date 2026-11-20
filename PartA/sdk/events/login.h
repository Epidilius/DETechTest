#pragma once

#include <cstdint>
#include <string_view>

#include "sdk/events/platform.h"
#include "sdk/schema/required.h"
#include "sdk/schema/trackable_event.h"

namespace tracking
{

    struct Login
    {
        static constexpr std::string_view kName = "LOGIN";
        static constexpr std::uint32_t kVersion = 1;

        Required<Platform> platform;

        static void Describe(auto& visit)
        {
            visit("platform", &Login::platform);
        }
    };

    static_assert(TrackableEvent<Login>);
} // namespace tracking
