// Stand-in for the HTTPS transport. Used by the demo and the tests only.
#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sdk/data/event_record.h"
#include "sdk/data/send_result.h"
#include "sdk/interfaces/transport.h"

namespace tracking
{

    // Hands the batch straight to a handler in this process. The handler is a function, so the SDK does not depend on the service's code.
    class InMemoryTransport final : public ITransport
    {
    public:
        explicit InMemoryTransport(std::function<SendResult(const std::vector<EventRecord>&, const std::optional<std::string>&)> handler) : handler_(std::move(handler))
        {
        }

        SendResult Send(const std::vector<EventRecord>& batch, const std::optional<std::string>& token, std::chrono::milliseconds) override
        {
            return handler_(batch, token);
        }

    private:
        std::function<SendResult(const std::vector<EventRecord>&, const std::optional<std::string>&)> handler_;
    };

} // namespace tracking
