#pragma once

#include <string_view>

namespace tracking
{
    enum class LoginFailure
    {
        InvalidCredentials,
        NetworkError,
        ServiceUnavailable
    };

    constexpr std::string_view ToString(LoginFailure failure)
    {
        switch (failure)
        {
            case LoginFailure::InvalidCredentials:
            {
                return "INVALID_CREDENTIALS";
            }
            case LoginFailure::NetworkError:
            {
                return "NETWORK_ERROR";
            }
            case LoginFailure::ServiceUnavailable:
            {
                return "SERVICE_UNAVAILABLE";
            }
        }
        return "INVALID";
    }
} // namespace tracking
