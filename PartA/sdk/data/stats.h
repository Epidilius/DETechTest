#pragma once

#include <cstdint>

namespace tracking
{
    // Cumulative since launch. tracked = sent + failed + still queued.
    struct Stats
    {
        std::uint64_t tracked = 0;
        std::uint64_t sent = 0;
        std::uint64_t dropped = 0;
        std::uint64_t failed = 0;
    };
} // namespace tracking
