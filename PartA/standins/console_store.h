// Stand-in for MongoDB. Used by the demo and the tests only.
#pragma once

#include <cstddef>
#include <map>
#include <mutex>
#include <ostream>
#include <set>
#include <span>
#include <string>

#include "service/data/accepted_event.h"
#include "service/data/insert_result.h"
#include "service/interfaces/store.h"

namespace tracking
{
    // Prints one plain line per event where a real store would insert a document.
    // In MongoDB this would be a unique index on `event_id` and an unordered insertMany, with a duplicate-key error (code 11000) counted as success. Here a set imitates that index.
    class ConsoleStore final : public IEventStore
    {
    public:
        explicit ConsoleStore(std::ostream& out) : out_(out)
        {
        }

        InsertResult Insert(std::span<const AcceptedEvent> events) override;

        std::size_t size() const
        {
            std::lock_guard lock(mutex_);
            return event_ids_.size();
        }
        // How many events are stored for each event name.
        std::map<std::string, std::size_t> CountsByType() const
        {
            std::lock_guard lock(mutex_);
            return counts_;
        }

    private:
        mutable std::mutex mutex_;
        std::ostream& out_;
        std::set<std::string> event_ids_;
        std::map<std::string, std::size_t> counts_;
    };

} // namespace tracking
