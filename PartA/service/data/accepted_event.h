#pragma once

#include <chrono>

#include "sdk/data/event_record.h"

namespace tracking
{
    struct AcceptedEvent
    {
        EventRecord record;
        std::chrono::system_clock::time_point received_at;
        bool authenticated = false;
    };
} // namespace tracking
