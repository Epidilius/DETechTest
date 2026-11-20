#pragma once

#include <chrono>
#include <string>
#include <string_view>

#include "sdk/data/event_record.h"
#include "sdk/data/identity.h"
#include "sdk/ids.h"
#include "sdk/schema/record_builder.h"
#include "sdk/schema/trackable_event.h"

namespace tracking
{
    // Turns a typed event into the generic record, and stamps event id, time, and identity

    template <TrackableEvent E>
    EventRecord MakeRecord(const E& event, const Identity& who, std::chrono::system_clock::time_point now)
    {
        EventRecord record;
        record.event_id = NewEventId(now);
        record.name = std::string(std::string_view(E::kName));
        record.version = E::kVersion;
        record.time = now;
        record.session_id = who.session_id;
        record.install_id = who.install_id;
        record.player_id = who.player_id;
        record.fields = FieldsOf(event);
        return record;
    }
} // namespace tracking
