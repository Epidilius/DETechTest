#include "sdk/ids.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "sdk/id_parts.h"

namespace tracking
{
    namespace
    {
        constexpr int kTimestampShift = 16;                   // the timestamp fills the top 48 bits of the high half
        constexpr std::uint64_t kBelowTimestampMask = 0xFFFF; // the 16 bits under it: the version, then 12 random bits
    } // namespace

    std::string NewEventId(std::chrono::system_clock::time_point now)
    {
        const std::int64_t milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        const std::uint64_t high = (static_cast<std::uint64_t>(milliseconds) << kTimestampShift) | (Random64() & kBelowTimestampMask);
        const std::uint64_t low = Random64();
        return FormatUuid(high, low, kUuidVersionTimeOrdered);
    }
} // namespace tracking
