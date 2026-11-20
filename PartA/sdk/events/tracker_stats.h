#pragma once

#include <cstdint>
#include <string_view>

#include "sdk/schema/required.h"
#include "sdk/schema/trackable_event.h"

namespace tracking
{
    // Sent by the SDK about itself at startup, every few minutes, and at shutdown. Counts are cumulative since launch.
    struct TrackerStats
    {
        static constexpr std::string_view kName = "TRACKER_STATS";
        static constexpr std::uint32_t kVersion = 1;

        Required<std::int64_t> tracked;
        Required<std::int64_t> sent;
        Required<std::int64_t> dropped;
        Required<std::int64_t> failed;

        static void Describe(auto& visit)
        {
            visit("tracked", &TrackerStats::tracked);
            visit("sent", &TrackerStats::sent);
            visit("dropped", &TrackerStats::dropped);
            visit("failed", &TrackerStats::failed);
        }
    };

    static_assert(TrackableEvent<TrackerStats>);
} // namespace tracking
