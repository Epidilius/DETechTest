#pragma once

#include <chrono>
#include <string>

namespace tracking
{
    // The event id is the sole identifier the SDK makes. Might be replaced by an existing service. 
    std::string NewEventId(std::chrono::system_clock::time_point now);
} // namespace tracking
