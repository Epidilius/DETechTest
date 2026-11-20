#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sdk/data/field_value.h"

namespace tracking
{
    // The generic form every event takes once it leaves the call site. The queue, the transport, and the service only ever see this type.
    struct EventRecord
    {
        std::string event_id;
        std::string session_id;
        std::string install_id;

        std::string name;
        std::uint32_t version = 0;

        // when Track() was called, UTC
        std::chrono::system_clock::time_point time;

        std::optional<std::string> player_id;

        std::vector<std::pair<std::string, FieldValue>> fields;
    };
} // namespace tracking
