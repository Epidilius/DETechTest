#pragma once

#include <optional>
#include <string>

namespace tracking
{
    // How the service checks an auth token. Production would ask the platform service.
    class IAuthVerifier
    {
    public:
        virtual ~IAuthVerifier() = default;
        virtual std::optional<std::string> Verify(const std::string& token) const = 0;
    };
} // namespace tracking
