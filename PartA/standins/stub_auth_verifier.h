// Stand-in for the platform service's server side, which checks auth tokens. Used by the demo and the tests only.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>

#include "service/interfaces/auth_verifier.h"

namespace tracking
{

    // Knows only the tokens it is told about with Add().
    class StubAuthVerifier final : public IAuthVerifier
    {
    public:
        void Add(std::string token, std::string player_id)
        {
            players_[std::move(token)] = std::move(player_id);
        }
        std::optional<std::string> Verify(const std::string& token) const override
        {
            const auto found = players_.find(token);
            if (found == players_.end())
            {
                return std::nullopt;
            }
            return found->second;
        }

    private:
        std::map<std::string, std::string> players_;
    };

} // namespace tracking
