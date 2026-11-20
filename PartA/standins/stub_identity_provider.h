// Stand-in for the platform service that supplies identity. Used by the demo and the tests only.
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "sdk/data/identity.h"
#include "sdk/id_parts.h"
#include "sdk/interfaces/identity_provider.h"

namespace tracking
{

    // It makes up a session id and an install id, and is told the player id and token by SignIn(). Each instance is one launch on a new install, because the install id is not saved.
    class StubIdentityProvider final : public IIdentityProvider
    {
    public:
        StubIdentityProvider() : session_id_(MakeSessionId()), install_id_(MakeInstallId())
        {
        }

        void SignIn(std::string player_id, std::string token)
        {
            std::lock_guard lock(mutex_);
            player_id_ = std::move(player_id);
            token_ = std::move(token);
        }
        void SignOut()
        {
            std::lock_guard lock(mutex_);
            player_id_.reset();
            token_.clear();
        }

        Identity Current() const override
        {
            std::lock_guard lock(mutex_);
            Identity identity;
            identity.session_id = session_id_;
            identity.install_id = install_id_;
            identity.player_id = player_id_;
            return identity;
        }
        std::optional<std::string> AuthToken(const std::string& player_id) const override
        {
            std::lock_guard lock(mutex_);
            if (player_id_.has_value() && *player_id_ == player_id)
            {
                return token_;
            }
            return std::nullopt;
        }

    private:
        static constexpr int kSessionIdDigits = 16;

        // 16 random hex digits.
        static std::string MakeSessionId()
        {
            const std::uint64_t random_bits = Random64();
            std::string session_id;
            AppendHex(session_id, random_bits, kSessionIdDigits);
            return session_id;
        }
        // A random UUID.
        static std::string MakeInstallId()
        {
            const std::uint64_t high = Random64();
            const std::uint64_t low = Random64();
            return FormatUuid(high, low, kUuidVersionRandom);
        }

        mutable std::mutex mutex_;
        const std::string session_id_;
        const std::string install_id_;
        std::optional<std::string> player_id_;
        std::string token_;
    };

} // namespace tracking
