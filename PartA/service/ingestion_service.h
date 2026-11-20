#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sdk/data/event_record.h"
#include "sdk/schema/field_spec.h"
#include "sdk/schema/schema_builder.h"
#include "sdk/schema/trackable_event.h"
#include "service/data/accepted_event.h"
#include "service/data/ingest_result.h"
#include "service/interfaces/auth_verifier.h"
#include "service/interfaces/store.h"

namespace tracking
{
    // The ingestion service: authenticate, validate, store. It treats every batch as untrusted. In production it would be its own API or other deployed service.
    class IngestionService
    {
    public:
        IngestionService(const IAuthVerifier& auth, IEventStore& store);
        IngestResult Ingest(const std::vector<EventRecord>& batch, const std::optional<std::string>& token);

        std::map<std::string, std::uint64_t> rejected() const
        {
            std::lock_guard lock(mutex_);
            return rejected_;
        }

    private:
        struct Sender
        {
            bool authorized = true;
            std::optional<std::string> player;
        };

        // Teaches the service one event. The schema is read from the same struct the game uses, so the two cannot disagree about the event's shape.
        template <TrackableEvent E>
        void Register()
        {
            const std::pair<std::string, std::uint32_t> key(std::string(std::string_view(E::kName)), E::kVersion);
            schemas_[key] = SchemaOf<E>();
        }

        void Reject(const std::string& reason, std::size_t count = 1);

        Sender IdentifySender(const std::optional<std::string>& token) const;
        IngestResult RefuseBatch(std::size_t count);
        std::vector<AcceptedEvent> ProcessEvents(const std::vector<EventRecord>& batch, const std::optional<std::string>& player);
        IngestResult StoreEvents(const std::vector<AcceptedEvent>& accepted);

        mutable std::mutex mutex_;

        const IAuthVerifier& auth_;
        IEventStore& store_;

        // Keyed by log_type and version.
        std::map<std::pair<std::string, std::uint32_t>, std::vector<FieldSpec>> schemas_;
        std::map<std::string, std::uint64_t> rejected_;
    };
} // namespace tracking
