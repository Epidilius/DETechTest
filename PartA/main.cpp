// Demo for Part A.
// Tracks the brief's three events, plus the two that happen before sign-in, and prints where each would be inserted into MongoDB.
// A second pass sends events through a transport that fails on purpose.
// A summary at the end says what was tracked, sent, and stored.
#include <chrono>
#include <cstddef>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "sdk/data_tracker.h"
#include "sdk/events/join_method.h"
#include "sdk/events/login_failure.h"
#include "sdk/events/mission_type.h"
#include "sdk/events/platform.h"
#include "sdk/events/squad_member.h"
#include "sdk/events/start_mission.h"
#include "service/ingestion_service.h"
#include "standins/console_store.h"
#include "standins/flaky_transport.h"
#include "standins/in_memory_transport.h"
#include "standins/stub_auth_verifier.h"
#include "standins/stub_identity_provider.h"

using namespace tracking;
using namespace std::chrono_literals;

namespace
{
    const std::string kPlayerId = "67f68ce387eb193fe6b5e0ca";
    const std::string kToken = "demo-token";
    const std::string kCrashedSessionId = "5d0c9b1e2a7f4e11";
    const std::string kMissionNode = "SolNode26";
    const int kPurchaseQuantity = 3;

    struct PassResult
    {
        std::string title;
        Stats stats;
        std::size_t sdk_own_events = 0;
        std::size_t sends = 0;
        std::string sends_note;
        std::size_t duplicates = 0;
    };

    void PrintPass(const PassResult& pass)
    {
        std::cout << pass.title << "\n";
        std::cout << "  events tracked ............. " << pass.stats.tracked;
        if (pass.sdk_own_events > 0)
        {
            std::cout << "  (" << pass.stats.tracked - pass.sdk_own_events << " from the game, " << pass.sdk_own_events << " stats reports from the SDK)";
        }
        std::cout << "\n";
        std::cout << "  acknowledged by the service  " << pass.stats.sent << "\n";
        std::cout << "  dropped, queue full ........ " << pass.stats.dropped << "\n";
        std::cout << "  failed, gave up ............ " << pass.stats.failed << "\n";
        std::cout << "  sends attempted ............ " << pass.sends << pass.sends_note << "\n";
        std::cout << "  duplicates the store refused " << pass.duplicates << "\n";
    }
} // namespace

int main()
{
    // The service side: an auth stub and a store that prints. The service registers its own events.
    StubAuthVerifier auth;
    auth.Add(kToken, kPlayerId);
    ConsoleStore store(std::cout);
    IngestionService service(auth, store);

    // The "network": a direct call into the service, counting what it reports.
    std::size_t sends = 0;
    std::size_t duplicates = 0;
    InMemoryTransport transport([&](const std::vector<EventRecord>& batch, const std::optional<std::string>& token)
    {
        ++sends;
        const IngestResult result = service.Ingest(batch, token);
        duplicates += result.duplicates;
        return ToSendResult(result);
    });

    StubIdentityProvider identity;
    std::vector<PassResult> passes;

    std::cout << "== Pass 1: every send succeeds ==\n";
    Config clean_config(identity, transport);
    clean_config.dispatcher.synchronous = true;
    DataTracker::Initialize(clean_config);

    // Before sign-in: no player yet, so these carry only session and install ids.
    DataTracker::TrackCrash({.crashed_session_id = kCrashedSessionId});
    DataTracker::TrackLoginFailed({.platform = Platform::Steam, .reason = LoginFailure::NetworkError});

    // The brief's three events.
    identity.SignIn(kPlayerId, kToken);
    DataTracker::TrackLogin({.platform = Platform::Steam});
    DataTracker::TrackStartMission({.mission_type = MissionType::Defense, .node = kMissionNode});
    DataTracker::TrackPurchasedItem({.item = "Forma", .quantity = kPurchaseQuantity});

    // The same event through the core function, with nested data.
    std::vector<SquadMember> squad;
    squad.push_back(SquadMember{.player_id = "67f68ce587eb193fe6b5e1ed", .join_method = JoinMethod::Party});
    squad.push_back(SquadMember{.player_id = "67f68ce587eb193fe6b5e3b2", .join_method = JoinMethod::Matchmaking});
    DataTracker::Track(StartMission{.mission_type = MissionType::Survival, .node = kMissionNode, .squad = squad});

    // Shutdown sends the final stats report, then returns the final counters.
    PassResult clean_pass;
    clean_pass.title = "Pass 1, every send succeeds";
    clean_pass.stats = DataTracker::Shutdown();
    clean_pass.sdk_own_events = store.CountsByType()["TRACKER_STATS"];
    clean_pass.sends = sends;
    clean_pass.duplicates = duplicates;
    passes.push_back(clean_pass);

    std::cout << "\n== Pass 2: the transport loses a reply, then drops a request ==\n";
    std::vector<Fault> faults;
    faults.push_back(Fault::LoseReply);
    faults.push_back(Fault::FailBeforeSend);
    FlakyTransport flaky(transport, faults);
    duplicates = 0;

    Config flaky_config(identity, flaky);
    flaky_config.dispatcher.initial_backoff = 10ms;
    flaky_config.dispatcher.report_stats = false;
    DataTracker::Initialize(flaky_config);
    DataTracker::TrackLogin({.platform = Platform::Steam});
    DataTracker::TrackStartMission({.mission_type = MissionType::Defense, .node = kMissionNode});
    DataTracker::TrackPurchasedItem({.item = "Forma", .quantity = kPurchaseQuantity});
    DataTracker::Flush(5s);

    PassResult flaky_pass;
    flaky_pass.title = "Pass 2, the transport fails on purpose";
    flaky_pass.stats = DataTracker::Shutdown();
    flaky_pass.sends = flaky.sends();
    flaky_pass.sends_note = "  (1 reply lost, 1 request dropped, 1 clean)";
    flaky_pass.duplicates = duplicates;
    passes.push_back(flaky_pass);

    // ---------------------------------------------------------------- summary
    std::cout << "\n== Summary ==\n";
    Stats total;
    for (const PassResult& pass : passes)
    {
        PrintPass(pass);
        total.tracked += pass.stats.tracked;
        total.sent += pass.stats.sent;
        total.dropped += pass.stats.dropped;
        total.failed += pass.stats.failed;
    }

    std::cout << "Events stored: " << store.size() << "\n  ";
    bool first = true;
    for (const auto& [name, count] : store.CountsByType())
    {
        std::cout << (first ? "" : ", ") << name << " " << count;
        first = false;
    }
    std::cout << "\n";

    const std::map<std::string, std::uint64_t> rejected = service.rejected();
    std::cout << "Rejected by the service: " << (rejected.empty() ? "none" : "") << "\n";
    for (const auto& [reason, count] : rejected)
    {
        std::cout << "  " << count << " x " << reason << "\n";
    }

    const bool exactly_once = total.tracked == store.size() && total.sent == total.tracked && total.dropped == 0 && total.failed == 0 && rejected.empty();
    std::cout << "Result: " << (exactly_once ? "every tracked event was stored exactly once." : "not every tracked event was stored; see the counts above.") << "\n";
    return exactly_once ? 0 : 1;
}
