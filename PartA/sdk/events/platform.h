#pragma once

#include <string_view>

namespace tracking
{
    enum class Platform
    {
        Steam,
        Epic,
        PlayStation,
        Xbox
    };

    constexpr std::string_view ToString(Platform platform)
    {
        switch (platform)
        {
            case Platform::Steam:
            {
                return "Steam";
            }
            case Platform::Epic:
            {
                return "Epic";
            }
            case Platform::PlayStation:
            {
                return "PlayStation";
            }
            case Platform::Xbox:
            {
                return "Xbox";
            }
        }

        return "INVALID";
    }
} // namespace tracking
