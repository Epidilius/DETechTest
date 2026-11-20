// Tests that run events through the whole pipeline: tracker, dispatcher, transport, ingestion service, and store.
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "sdk/data_tracker.h"
#include "sdk/events/crash.h"
#include "sdk/events/login.h"
#include "sdk/events/login_failed.h"
#include "sdk/events/login_failure.h"
#include "sdk/events/platform.h"
#include "sdk/events/purchased_item.h"
#include "service/ingestion_service.h"
#include "standins/console_store.h"
#include "standins/flaky_transport.h"
#include "standins/in_memory_transport.h"
#include "standins/stub_auth_verifier.h"
#include "standins/stub_identity_provider.h"
#include "tests/check.h"

using namespace tracking;
using namespace std::chrono_literals;

namespace
{

    const std::string kPlayer = "67f68ce387eb193fe6b5e0ca";
    const std::string kToken = "token-1";

    // The service side plus an in-memory transport, with everything the service reports added up so tests can assert on it.
    struct Rig
    {
        StubAuthVerifier auth;
        std::ostringstream out;
        ConsoleStore store{out};
        IngestionService service{auth, store};
        std::size_t duplicates = 0;
        std::size_t rejected = 0;
        InMemoryTransport transport{[this](const std::vector<EventRecord>& batch, const std::optional<std::string>& token)
        {
            const IngestResult result = service.Ingest(batch, token);
            duplicates += result.duplicates;
            rejected += result.rejected;
            return ToSendResult(result);
        }};
        StubIdentityProvider identity;

        Rig()
        {
            auth.Add(kToken, kPlayer);
        }

        std::vector<std::string> Lines() const
        {
            std::vector<std::string> lines;
            std::istringstream in(out.str());
            for (std::string line; std::getline(in, line);)
            {
                lines.push_back(line);
            }
            return lines;
        }
    };

    struct Pipeline
    {
        Pipeline(ITransport& transport, IIdentityProvider& identity, DispatcherConfig config) : dispatcher(transport, identity, config), tracker(identity, dispatcher)
        {
        }
        Dispatcher dispatcher;
        Tracker tracker;
    };

    // Sends on the caller's thread with no waiting, so results are exact.
    DispatcherConfig Sync()
    {
        DispatcherConfig config;
        config.initial_backoff = 0ms;
        config.report_stats = false;
        config.synchronous = true;
        return config;
    }

    // Sends on the worker thread, as a game would.
    DispatcherConfig Async()
    {
        DispatcherConfig config;
        config.initial_backoff = 1ms;
        config.report_stats = false;
        return config;
    }

    bool Contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    // Holds every send until Open(), and records what each batch looked like.
    class GateTransport final : public ITransport
    {
    public:
        struct Seen
        {
            std::size_t size = 0;
            std::optional<std::string> token;
            bool one_identity = true;
        };

        explicit GateTransport(ITransport& inner) : inner_(inner)
        {
        }

        SendResult Send(const std::vector<EventRecord>& batch, const std::optional<std::string>& token, std::chrono::milliseconds deadline) override
        {
            {
                std::unique_lock lock(mutex_);
                ++entered_;
                cv_.notify_all();
                cv_.wait(lock, [this]
                {
                    return open_;
                });
                bool one_identity = true;
                for (const auto& record : batch)
                {
                    one_identity = one_identity && record.player_id == batch.front().player_id;
                }
                Seen seen;
                seen.size = batch.size();
                seen.token = token;
                seen.one_identity = one_identity;
                seen_.push_back(seen);
            }
            return inner_.Send(batch, token, deadline);
        }

        void Open()
        {
            std::lock_guard lock(mutex_);
            open_ = true;
            cv_.notify_all();
        }
        void WaitUntilEntered(int count)
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [&]
            {
                return entered_ >= count;
            });
        }
        std::vector<Seen> seen()
        {
            std::lock_guard lock(mutex_);
            return seen_;
        }

    private:
        ITransport& inner_;
        std::mutex mutex_;
        std::condition_variable cv_;
        bool open_ = false;
        int entered_ = 0;
        std::vector<Seen> seen_;
    };

    void TestEndToEndDocuments()
    {
        Rig rig;
        const DispatcherConfig config = Sync();
        Pipeline pipeline(rig.transport, rig.identity, config);

        pipeline.tracker.Track(Crash{.crashed_session_id = "abc"}); // before sign-in
        rig.identity.SignIn(kPlayer, kToken);
        pipeline.tracker.Track(Login{.platform = Platform::Steam});
        pipeline.tracker.Track(PurchasedItem{.item = "Forma", .quantity = 3});

        const auto lines = rig.Lines();
        CHECK(lines.size() == 3);
        if (lines.size() != 3)
        {
            return;
        }

        const Identity identity = rig.identity.Current();
        const std::string expected_install = " install=" + identity.install_id + " ";
        const std::string expected_session = " session=" + identity.session_id + " ";
        const std::string expected_player = " player=" + kPlayer + " ";

        // Before sign-in: no player, marked unauthenticated, still grouped by install.
        CHECK(Contains(lines[0], "CRASH v1 "));
        CHECK(Contains(lines[0], " authenticated=false "));
        CHECK(Contains(lines[0], " player=none "));
        CHECK(Contains(lines[0], expected_install));

        // After sign-in: the player is on the line, and the event's own fields follow the envelope.
        CHECK(Contains(lines[1], "LOGIN v1 "));
        CHECK(Contains(lines[1], expected_player));
        CHECK(Contains(lines[1], " authenticated=true "));
        CHECK(Contains(lines[2], " time_ms="));
        CHECK(Contains(lines[2], "PURCHASED_ITEM v1 "));
        CHECK(Contains(lines[2], R"(fields={item: "Forma", quantity: 3})"));
        CHECK(Contains(lines[2], expected_session));

        const Stats stats = pipeline.dispatcher.stats();
        CHECK(stats.tracked == 3 && stats.sent == 3 && stats.dropped == 0 && stats.failed == 0);
    }

    void TestLostReplyIsNotStoredTwice()
    {
        Rig rig;
        const std::vector<Fault> plan(1, Fault::LoseReply);
        FlakyTransport flaky(rig.transport, plan);
        const DispatcherConfig config = Sync();
        Pipeline pipeline(flaky, rig.identity, config);

        pipeline.tracker.Track(Crash{.crashed_session_id = "a"}); // delivered, reply lost, sent again
        pipeline.tracker.Track(Crash{.crashed_session_id = "b"});

        CHECK(flaky.sends() == 3);
        CHECK(rig.store.size() == 2);
        CHECK(rig.duplicates == 1);
        CHECK(pipeline.dispatcher.stats().sent == 2 && pipeline.dispatcher.stats().failed == 0);
    }

    void TestRetryThenSuccess()
    {
        Rig rig;
        const std::vector<Fault> plan(2, Fault::FailBeforeSend);
        FlakyTransport flaky(rig.transport, plan);
        const DispatcherConfig config = Sync();
        Pipeline pipeline(flaky, rig.identity, config);

        pipeline.tracker.Track(Crash{.crashed_session_id = "a"});

        CHECK(flaky.sends() == 3);
        CHECK(rig.store.size() == 1);
        CHECK(pipeline.dispatcher.stats().sent == 1);
    }

    void TestRetriesExhaustedIsCounted()
    {
        Rig rig;
        const bool repeat_last = true;
        const std::vector<Fault> plan(1, Fault::FailBeforeSend);
        FlakyTransport down(rig.transport, plan, repeat_last);
        DispatcherConfig config = Sync();
        config.max_attempts = 3;
        Pipeline pipeline(down, rig.identity, config);

        pipeline.tracker.Track(Crash{.crashed_session_id = "a"});

        CHECK(down.sends() == 3);
        CHECK(rig.store.size() == 0);
        const Stats stats = pipeline.dispatcher.stats();
        CHECK(stats.tracked == 1 && stats.sent == 0 && stats.failed == 1);
    }

    void TestFullQueueDropsAndCounts()
    {
        Rig rig;
        GateTransport gate(rig.transport);
        DispatcherConfig config = Async();
        config.max_queue = 2;
        Pipeline pipeline(gate, rig.identity, config);

        pipeline.tracker.Track(Crash{.crashed_session_id = "0"});
        gate.WaitUntilEntered(1); // the worker is now stuck in Send with that event
        for (int i = 1; i < 10; ++i)
        {
            const std::string crashed_session = std::to_string(i);
            pipeline.tracker.Track(Crash{.crashed_session_id = crashed_session});
        }

        Stats stats = pipeline.dispatcher.stats();
        CHECK(stats.tracked == 3); // one in flight, two queued
        CHECK(stats.dropped == 7);

        gate.Open();
        CHECK(pipeline.dispatcher.Flush(5s));
        stats = pipeline.dispatcher.stats();
        CHECK(stats.sent == 3 && stats.failed == 0);
        CHECK(rig.store.size() == 3);
    }

    void TestBatchHoldsOneIdentity()
    {
        Rig rig;
        GateTransport gate(rig.transport);
        const DispatcherConfig config = Async();
        Pipeline pipeline(gate, rig.identity, config);

        pipeline.tracker.Track(Crash{.crashed_session_id = "a"});
        gate.WaitUntilEntered(1);
        pipeline.tracker.Track(LoginFailed{.platform = Platform::Steam, .reason = LoginFailure::NetworkError});
        rig.identity.SignIn(kPlayer, kToken);
        pipeline.tracker.Track(Login{.platform = Platform::Steam});
        pipeline.tracker.Track(PurchasedItem{.item = "Forma", .quantity = 3});
        gate.Open();
        CHECK(pipeline.dispatcher.Flush(5s));

        // Queued as [anonymous, player, player]: cut where the identity changes.
        const auto seen = gate.seen();
        CHECK(seen.size() == 3);
        if (seen.size() != 3)
        {
            return;
        }
        CHECK(seen[0].size == 1 && seen[0].token.has_value() == false);
        CHECK(seen[1].size == 1 && seen[1].token.has_value() == false);
        CHECK(seen[2].size == 2 && seen[2].token == kToken);
        for (const auto& batch : seen)
        {
            CHECK(batch.one_identity);
        }
        CHECK(rig.store.size() == 4 && rig.rejected == 0);
    }

    void TestSignedOutEventsCannotBeSent()
    {
        Rig rig;
        GateTransport gate(rig.transport);
        const DispatcherConfig config = Async();
        Pipeline pipeline(gate, rig.identity, config);
        rig.identity.SignIn(kPlayer, kToken);

        pipeline.tracker.Track(Login{.platform = Platform::Steam});
        gate.WaitUntilEntered(1); // this one already has its token
        pipeline.tracker.Track(PurchasedItem{.item = "Forma", .quantity = 3});
        rig.identity.SignOut(); // the queued one can no longer get a token
        gate.Open();
        CHECK(pipeline.dispatcher.Flush(5s));

        const Stats stats = pipeline.dispatcher.stats();
        CHECK(stats.sent == 1 && stats.failed == 1);
        CHECK(rig.store.size() == 1);
    }

    void TestServiceTrustsNothing()
    {
        Rig rig;
        const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        Identity anonymous;
        anonymous.session_id = "session";
        anonymous.install_id = "install";
        Identity someone_else = anonymous;
        someone_else.player_id = "ffffffffffffffffffffffff";

        // A valid token for one player cannot store events for another.
        const EventRecord wrong_player_record = MakeRecord(Login{.platform = Platform::Steam}, someone_else, now);
        const std::vector<EventRecord> wrong_player(1, wrong_player_record);
        IngestResult result = rig.service.Ingest(wrong_player, kToken);
        CHECK(result.authorized && result.stored == 0 && result.rejected == 1);

        // A token the auth platform does not know refuses the whole batch.
        const EventRecord forged_record = MakeRecord(Login{.platform = Platform::Steam}, anonymous, now);
        const std::vector<EventRecord> forged(1, forged_record);
        result = rig.service.Ingest(forged, std::string("forged"));
        CHECK(result.authorized == false && result.stored == 0);
        CHECK(ToSendResult(result) == SendResult::Rejected);

        // An event the service was never taught, and a version it does not know.
        EventRecord mystery = MakeRecord(Login{.platform = Platform::Steam}, anonymous, now);
        mystery.name = "MYSTERY";
        EventRecord future = MakeRecord(Login{.platform = Platform::Steam}, anonymous, now);
        future.version = 2;
        std::vector<EventRecord> unknown;
        unknown.push_back(mystery);
        unknown.push_back(future);
        result = rig.service.Ingest(unknown, std::nullopt);
        CHECK(result.stored == 0 && result.rejected == 2);

        // The right event with the wrong shape.
        EventRecord bad = MakeRecord(PurchasedItem{.item = "Forma", .quantity = 3}, anonymous, now);
        bad.fields = std::vector<std::pair<std::string, FieldValue>>{{"item", "Forma"}, {"quantity", "three"}};
        const std::vector<EventRecord> bad_batch(1, bad);
        result = rig.service.Ingest(bad_batch, std::nullopt);
        CHECK(result.stored == 0 && result.rejected == 1);

        const auto reasons = rig.service.rejected();
        CHECK(reasons.count("player does not match token") == 1);
        CHECK(reasons.count("invalid token") == 1);
        CHECK(reasons.count("unknown event MYSTERY v1") == 1);
        CHECK(reasons.count("unknown event LOGIN v2") == 1);
        CHECK(reasons.count("PURCHASED_ITEM: wrong type at quantity") == 1);
        CHECK(rig.store.size() == 0);
    }

    void TestStatsReportsAtStartupAndShutdown()
    {
        Rig rig;
        DispatcherConfig config = Sync();
        config.report_stats = true;
        {
            Pipeline pipeline(rig.transport, rig.identity, config);
            pipeline.tracker.Track(Crash{.crashed_session_id = "a"});
            pipeline.tracker.Track(Crash{.crashed_session_id = "b"});
        } // the destructor stops the dispatcher, which sends the final report

        const auto lines = rig.Lines();
        CHECK(lines.size() == 4);
        if (lines.size() != 4)
        {
            return;
        }
        CHECK(Contains(lines.front(), "TRACKER_STATS v1 "));
        CHECK(Contains(lines.front(), "fields={tracked: 0, sent: 0, dropped: 0, failed: 0}"));
        // Cumulative: the startup report and the two events have all been sent.
        CHECK(Contains(lines.back(), "TRACKER_STATS v1 "));
        CHECK(Contains(lines.back(), "fields={tracked: 3, sent: 3, dropped: 0, failed: 0}"));
    }

    void TestTrackFromManyThreads()
    {
        Rig rig;
        DispatcherConfig config = Async();
        config.max_queue = 5000;
        Pipeline pipeline(rig.transport, rig.identity, config);

        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t)
        {
            threads.emplace_back([&pipeline]
            {
                for (int i = 0; i < 250; ++i)
                {
                    const std::string crashed_session = std::to_string(i);
                    pipeline.tracker.Track(Crash{.crashed_session_id = crashed_session});
                }
            });
        }
        for (auto& thread : threads)
        {
            thread.join();
        }
        CHECK(pipeline.dispatcher.Flush(10s));

        const Stats stats = pipeline.dispatcher.stats();
        CHECK(stats.tracked == 1000 && stats.sent == 1000 && stats.dropped == 0 && stats.failed == 0);
        CHECK(rig.store.size() == 1000);
    }

    void TestShutdownDoesNotWaitOutABackoff()
    {
        Rig rig;
        const bool repeat_last = true;
        const std::vector<Fault> plan(1, Fault::FailBeforeSend);
        FlakyTransport down(rig.transport, plan, repeat_last);
        DispatcherConfig config = Async();
        config.initial_backoff = 10s; // far longer than the test will wait
        Pipeline pipeline(down, rig.identity, config);

        pipeline.tracker.Track(Crash{.crashed_session_id = "a"});
        for (int i = 0; i < 2000 && down.sends() == 0; ++i)
        {
            std::this_thread::sleep_for(1ms);
        }
        CHECK(down.sends() == 1); // the worker is now sleeping in its backoff

        const auto started = std::chrono::steady_clock::now();
        pipeline.dispatcher.Stop(50ms);
        CHECK(std::chrono::steady_clock::now() - started < 2s);
        CHECK(pipeline.dispatcher.stats().failed == 1);

        pipeline.tracker.Track(Crash{.crashed_session_id = "late"}); // after Stop: a counted no-op
        CHECK(pipeline.dispatcher.stats().dropped == 1);
    }

    void TestFacadeIgnoresCallsWhenNotRunning()
    {
        Rig rig;
        const auto ignored_before = DataTracker::IgnoredCalls();
        DataTracker::TrackLogin({.platform = Platform::Steam}); // before Initialize
        CHECK(DataTracker::IgnoredCalls() == ignored_before + 1);

        Config config(rig.identity, rig.transport);
        config.dispatcher = Sync();
        DataTracker::Initialize(config);
        DataTracker::TrackLoginFailed({.platform = Platform::Xbox, .reason = LoginFailure::ServiceUnavailable});
        DataTracker::Track(Crash{.crashed_session_id = "a"});
        CHECK(DataTracker::GetStats().sent == 2);
        const Stats final_stats = DataTracker::Shutdown();
        CHECK(final_stats.tracked == 2 && final_stats.sent == 2);
        CHECK(rig.store.CountsByType().at("LOGIN_FAILED") == 1 && rig.store.CountsByType().at("CRASH") == 1);

        DataTracker::TrackPurchasedItem({.item = "Forma", .quantity = 3}); // after Shutdown
        CHECK(DataTracker::IgnoredCalls() == ignored_before + 2);
        CHECK(rig.store.size() == 2);
    }

} // namespace

void RunPipelineTests()
{
    TestEndToEndDocuments();
    TestLostReplyIsNotStoredTwice();
    TestRetryThenSuccess();
    TestRetriesExhaustedIsCounted();
    TestFullQueueDropsAndCounts();
    TestBatchHoldsOneIdentity();
    TestSignedOutEventsCannotBeSent();
    TestServiceTrustsNothing();
    TestStatsReportsAtStartupAndShutdown();
    TestTrackFromManyThreads();
    TestShutdownDoesNotWaitOutABackoff();
    TestFacadeIgnoresCallsWhenNotRunning();
}
