#pragma once

#include <span>

#include "service/data/accepted_event.h"
#include "service/data/insert_result.h"

namespace tracking
{
    // Where accepted events go. Owns de-duplication. Production would implement its own MongoDB (or other persistent data) store
    class IEventStore
    {
    public:
        virtual ~IEventStore() = default;
        virtual InsertResult Insert(std::span<const AcceptedEvent> events) = 0;
    };
} // namespace tracking
