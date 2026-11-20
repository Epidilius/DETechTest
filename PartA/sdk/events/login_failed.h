#pragma once

#include <cstdint>
#include <string_view>

#include "sdk/events/login_failure.h"
#include "sdk/events/platform.h"
#include "sdk/schema/required.h"
#include "sdk/schema/trackable_event.h"

namespace tracking
{
    struct LoginFailed
    {
        static constexpr std::string_view kName = "LOGIN_FAILED";
        static constexpr std::uint32_t kVersion = 1;

        Required<Platform> platform;
        Required<LoginFailure> reason;

        static void Describe(auto& visit)
        {
            visit("platform", &LoginFailed::platform);
            visit("reason", &LoginFailed::reason);
        }
    };

    static_assert(TrackableEvent<LoginFailed>);
} // namespace tracking
