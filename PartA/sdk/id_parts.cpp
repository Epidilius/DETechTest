#include "sdk/id_parts.h"

#include <random>

namespace tracking
{
    namespace
    {
        constexpr char kHexDigits[] = "0123456789abcdef";
        constexpr int kBitsPerHexDigit = 4;
        constexpr std::uint64_t kHexDigitMask = 0xF;
        constexpr int kBitsPerSeedWord = 32;

        // A UUID is written as 8-4-4-4-12 hex digits, taken from two 64-bit halves.
        constexpr int kUuidLength = 36;
        constexpr int kUuidTimeLowDigits = 8;
        constexpr int kUuidGroupDigits = 4;
        constexpr int kUuidNodeDigits = 12;
        constexpr int kUuidTimeLowShift = 32;
        constexpr int kUuidTimeMidShift = 16;
        constexpr int kUuidClockShift = 48;
        constexpr std::uint64_t kUuidGroupMask = 0xFFFF;
        constexpr std::uint64_t kUuidNodeMask = 0xFFFFFFFFFFFF;

        // The version is the top digit of the third group. The variant is the top two bits of the low half.
        constexpr int kUuidVersionShift = 12;
        constexpr std::uint64_t kUuidVersionMask = 0xF000;
        constexpr std::uint64_t kUuidVariant = 0x8000000000000000;
        constexpr std::uint64_t kUuidVariantMask = 0xC000000000000000;

        std::uint64_t RandomSeed()
        {
            std::random_device device;
            const std::uint64_t low = device();
            const std::uint64_t high = device();
            return low ^ (high << kBitsPerSeedWord);
        }

    } // namespace

    std::uint64_t Random64()
    {
        // One generator per thread, so no lock is taken on the caller's thread.
        thread_local std::mt19937_64 generator(RandomSeed());
        return generator();
    }

    void AppendHex(std::string& out, std::uint64_t value, int digits)
    {
        for (int shift = (digits - 1) * kBitsPerHexDigit; shift >= 0; shift -= kBitsPerHexDigit)
        {
            out.push_back(kHexDigits[(value >> shift) & kHexDigitMask]);
        }
    }

    std::string FormatUuid(std::uint64_t high, std::uint64_t low, int version)
    {
        const std::uint64_t versioned_high = (high & ~kUuidVersionMask) | (static_cast<std::uint64_t>(version) << kUuidVersionShift);
        const std::uint64_t variant_low = (low & ~kUuidVariantMask) | kUuidVariant;

        std::string out;
        
        out.reserve(kUuidLength);
        AppendHex(out, versioned_high >> kUuidTimeLowShift, kUuidTimeLowDigits);
        out.push_back('-');
        AppendHex(out, (versioned_high >> kUuidTimeMidShift) & kUuidGroupMask, kUuidGroupDigits);
        out.push_back('-');
        AppendHex(out, versioned_high & kUuidGroupMask, kUuidGroupDigits);
        out.push_back('-');
        AppendHex(out, variant_low >> kUuidClockShift, kUuidGroupDigits);
        out.push_back('-');
        AppendHex(out, variant_low & kUuidNodeMask, kUuidNodeDigits);
        
        return out;
    }
} // namespace tracking
