// A transport that fails on purpose, so retries and de-duplication can be shown working. Used by the demo and the tests only.
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sdk/data/event_record.h"
#include "sdk/data/send_result.h"
#include "sdk/interfaces/transport.h"
#include "standins/fault.h"

namespace tracking
{

    // Wraps another transport and applies one fault per send, in order. After the plan runs out, every send passes through. `repeat_last` keeps applying the final fault instead, which models a service that stays down.
    class FlakyTransport final : public ITransport
    {
    public:
        FlakyTransport(ITransport& inner, std::vector<Fault> plan, bool repeat_last = false) : inner_(inner), plan_(std::move(plan)), repeat_last_(repeat_last)
        {
        }

        SendResult Send(const std::vector<EventRecord>& batch, const std::optional<std::string>& token, std::chrono::milliseconds deadline) override
        {
            switch (NextFault())
            {
                case Fault::None:
                {
                    return inner_.Send(batch, token, deadline);
                }
                case Fault::FailBeforeSend:
                {
                    return SendResult::Retry;
                }
                case Fault::LoseReply:
                {
                    inner_.Send(batch, token, deadline);
                    return SendResult::Retry;
                }
            }
            return SendResult::Retry;
        }

        std::size_t sends() const
        {
            return sends_;
        }

    private:
        Fault NextFault()
        {
            const std::size_t index = sends_++;
            if (index < plan_.size())
            {
                return plan_[index];
            }
            if (repeat_last_ && plan_.empty() == false)
            {
                return plan_.back();
            }
            return Fault::None;
        }

        ITransport& inner_;
        std::vector<Fault> plan_;
        bool repeat_last_;
        std::atomic<std::size_t> sends_{0};
    };

} // namespace tracking
