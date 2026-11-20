#pragma once

#include <cstdint>
#include <string>

namespace tracking
{
    // The pieces that id generators share (odds and ends). Functions are safe to call from any thread.

    constexpr int kUuidVersionRandom = 4;      // every bit is random
    constexpr int kUuidVersionTimeOrdered = 7; // starts with a millisecond timestamp

    std::uint64_t Random64();

    // Appends the lowest `digits` hex digits of `value`, most significant first.
    void AppendHex(std::string& out, std::uint64_t value, int digits);

    // Writes 128 bits as a UUID: 8-4-4-4-12 hex digits. The version and variant bits are overwritten, so the caller can pass random bits in their place.
    std::string FormatUuid(std::uint64_t high, std::uint64_t low, int version);

} // namespace tracking
