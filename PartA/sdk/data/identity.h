#pragma once

#include <optional>
#include <string>

namespace tracking
{
    // Who an event belongs to. Callers never pass ids to to the SDK, nor does the SDK does make them. Instead, we request them from an existing service that implements IIdentityProvider.
    struct Identity
    {
        std::string session_id;
        std::string install_id;
        std::optional<std::string> player_id;
    };
} // namespace tracking
