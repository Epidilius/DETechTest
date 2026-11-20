#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "sdk/data/event_record.h"
#include "sdk/data/send_result.h"

namespace tracking
{
    // The interface that hides the network. Production would implement over HTTPS.
    class ITransport
    {
    public:
        virtual ~ITransport() = default;

        // `token` is empty for a batch tracked before sign-in. `deadline` is how long the caller will wait, so a shutdown can bound its flush.
        virtual SendResult Send(const std::vector<EventRecord>& batch, const std::optional<std::string>& token, std::chrono::milliseconds deadline) = 0;
    };
} // namespace tracking
