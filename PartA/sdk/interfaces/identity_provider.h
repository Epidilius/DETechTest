#pragma once

#include <optional>
#include <string>

#include "sdk/data/identity.h"

namespace tracking
{
    // Stands for the existing platform service that supplies auth and identity data
    class IIdentityProvider
    {
    public:
        virtual ~IIdentityProvider() = default;

        // Read when Track() is called, so an event keeps the identity it had then. The session id and install id must be set before sign-in, because a failed login is tracked with them.
        virtual Identity Current() const = 0;

        // Read when a batch is sent, so a long-queued event does not carry a stale token. Empty if that player is no longer signed in.
        virtual std::optional<std::string> AuthToken(const std::string& player_id) const = 0;
    };
} // namespace tracking
