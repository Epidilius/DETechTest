#pragma once

#include <chrono>
#include <cstddef>

namespace tracking
{
    struct DispatcherConfig
    {
        std::size_t max_queue = 1024;
        std::size_t max_batch = 32;
        int max_attempts = 4;

        std::chrono::milliseconds initial_backoff{100};
        std::chrono::milliseconds send_deadline{2000};
        std::chrono::milliseconds shutdown_timeout{2000};
        std::chrono::milliseconds stats_interval{std::chrono::minutes(5)};

        // send TRACKER_STATS at startup, on the interval, and at shutdown
        bool report_stats = true;

        // Send on the caller's thread, one event per batch. Used to keep the demo and tests deterministic. Would not exist otherwise.
        bool synchronous = false;
    };
} // namespace tracking
