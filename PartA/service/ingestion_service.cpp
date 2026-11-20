#include "service/ingestion_service.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sdk/events/crash.h"
#include "sdk/events/login.h"
#include "sdk/events/login_failed.h"
#include "sdk/events/purchased_item.h"
#include "sdk/events/start_mission.h"
#include "sdk/events/tracker_stats.h"
#include "service/data/accepted_event.h"
#include "service/data/ingest_result.h"
#include "service/data/insert_result.h"
#include "service/validator.h"

namespace tracking
{
    IngestionService::IngestionService(const IAuthVerifier& auth, IEventStore& store) : auth_(auth), store_(store)
    {
        // An event that is not registered here is refused as unknown.
        Register<Login>();
        Register<StartMission>();
        Register<PurchasedItem>();
        Register<Crash>();
        Register<LoginFailed>();
        Register<TrackerStats>();
    }

    void IngestionService::Reject(const std::string& reason, std::size_t count)
    {
        std::lock_guard lock(mutex_);
        rejected_[reason] += count;
    }

    IngestResult IngestionService::Ingest(const std::vector<EventRecord>& batch, const std::optional<std::string>& token)
    {
        const Sender sender = IdentifySender(token);
        if (sender.authorized == false)
        {
            return RefuseBatch(batch.size());
        }

        const std::vector<AcceptedEvent> accepted = ProcessEvents(batch, sender.player);

        IngestResult result = StoreEvents(accepted);
        result.rejected = batch.size() - accepted.size();

        return result;
    }

    IngestionService::Sender IngestionService::IdentifySender(const std::optional<std::string>& token) const
    {
        Sender sender;
        if (token.has_value())
        {
            sender.player = auth_.Verify(*token);
            sender.authorized = sender.player.has_value();
        }

        return sender;
    }

    IngestResult IngestionService::RefuseBatch(std::size_t count)
    {
        Reject("invalid token", count);
        IngestResult result;
        result.authorized = false;
        result.rejected = count;

        return result;
    }

    std::vector<AcceptedEvent> IngestionService::ProcessEvents(const std::vector<EventRecord>& batch, const std::optional<std::string>& player)
    {
        const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        std::vector<AcceptedEvent> accepted;
        accepted.reserve(batch.size());

        for (const EventRecord& record : batch)
        {
            if (record.player_id.has_value() && (player.has_value() == false || *player != *record.player_id))
            {
                Reject("player does not match token");
                continue;
            }

            const std::pair<std::string, std::uint32_t> key(record.name, record.version);
            const auto schema = schemas_.find(key);
            if (schema == schemas_.end())
            {
                const std::string reason = "unknown event " + record.name + " v" + std::to_string(record.version);
                Reject(reason);
                continue;
            }

            const std::optional<std::string> problem = Validator::Validate(schema->second, record.fields);
            if (problem.has_value())
            {
                const std::string reason = record.name + ": " + *problem;
                Reject(reason);
                continue;
            }

            AcceptedEvent event;
            event.record = record;
            event.received_at = now;
            event.authenticated = record.player_id.has_value();
            accepted.push_back(std::move(event));
        }

        return accepted;
    }

    IngestResult IngestionService::StoreEvents(const std::vector<AcceptedEvent>& accepted)
    {
        const InsertResult inserted = store_.Insert(accepted);
        IngestResult result;
        result.stored = inserted.stored;
        result.duplicates = inserted.duplicates;

        return result;
    }
} // namespace tracking
