#pragma once

#include <chrono>
#include <utility>

#include "sdk/data/event_record.h"
#include "sdk/data/identity.h"
#include "sdk/data/stats.h"
#include "sdk/dispatch.h"
#include "sdk/envelope.h"
#include "sdk/interfaces/identity_provider.h"
#include "sdk/schema/trackable_event.h"

namespace tracking
{
    class Tracker
    {
    public:
        Tracker(IIdentityProvider& identity, Dispatcher& dispatcher) : identity_(identity), dispatcher_(dispatcher)
        {
        }

        // Safe from any thread. Does no IO. Builds a record and hands it to the dispatcher. 
        template <TrackableEvent E>
        void Track(const E& event) noexcept
        {
            try
            {
                const Identity identity = identity_.Current();
                const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
                EventRecord record = MakeRecord(event, identity, now);
                dispatcher_.Enqueue(std::move(record));
            }
            catch (...)
            {
                dispatcher_.CountFailed();
            }
        }

        Stats stats() const
        {
            return dispatcher_.stats();
        }

    private:
        IIdentityProvider& identity_;
        Dispatcher& dispatcher_;
    };
} // namespace tracking
