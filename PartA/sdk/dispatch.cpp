#include "sdk/dispatch.h"

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sdk/data/event_record.h"
#include "sdk/data/identity.h"
#include "sdk/data/send_result.h"
#include "sdk/data/stats.h"
#include "sdk/envelope.h"
#include "sdk/events/tracker_stats.h"

namespace tracking
{
    namespace
    {
        // Each failed attempt waits twice as long as the last
        constexpr int kBackoffMultiplier = 2; 
    }

    Dispatcher::Dispatcher(ITransport& transport, IIdentityProvider& identity, DispatcherConfig config) : transport_(transport), identity_(identity), config_(config)
    {
        if (config_.synchronous == false)
        {
            worker_ = std::jthread([this](std::stop_token stop)
            {
                Run(stop);
            });
        }
        
        if (config_.report_stats)
        {
            ReportStats();
        }
    }

    Dispatcher::~Dispatcher()
    {
        Stop(config_.shutdown_timeout);
    }

    void Dispatcher::Enqueue(EventRecord record) noexcept
    {
        try
        {
            if (accepting_.load() == false)
            {
                ++dropped_;
                return;
            }
            if (config_.synchronous)
            {
                ++tracked_;
                std::vector<EventRecord> batch;
                const std::stop_token no_stop;

                batch.push_back(std::move(record));
                std::lock_guard lock(send_mutex_);
                
                SendWithRetry(batch, no_stop);
                return;
            }
            {
                std::lock_guard lock(mutex_);
                if (queue_.size() >= config_.max_queue)
                {
                    ++dropped_;
                    return;
                }

                queue_.push_back(std::move(record));
                ++tracked_;
            }
            cv_.notify_one();
        }
        catch (...)
        {
            ++failed_;
        }
    }

    bool Dispatcher::Flush(std::chrono::milliseconds timeout)
    {
        if (config_.synchronous)
        {
            return true;
        }

        std::unique_lock lock(mutex_);
        return idle_cv_.wait_for(lock, timeout, [this]
        {
            return queue_.empty() && in_flight_ == 0;
        });
    }

    void Dispatcher::Stop(std::chrono::milliseconds timeout)
    {
        if (stopped_.exchange(true))
        {
            return;
        }
        if (config_.report_stats)
        {
            ReportStats();
        }

        accepting_.store(false);
        Flush(timeout);
        if (worker_.joinable())
        {
            worker_.request_stop();
            worker_.join();
        }

        std::lock_guard lock(mutex_);
        failed_ += queue_.size();
        queue_.clear();
    }

    void Dispatcher::ReportStats() noexcept
    {
        try
        {
            const Stats now = stats();
            const TrackerStats report{.tracked = static_cast<std::int64_t>(now.tracked), .sent = static_cast<std::int64_t>(now.sent), .dropped = static_cast<std::int64_t>(now.dropped), .failed = static_cast<std::int64_t>(now.failed)};
            const Identity identity = identity_.Current();
            const std::chrono::system_clock::time_point reported_at = std::chrono::system_clock::now();

            EventRecord record = MakeRecord(report, identity, reported_at);
            Enqueue(std::move(record));
        }
        catch (...)
        {
            ++failed_;
        }
    }

    Stats Dispatcher::stats() const
    {
        Stats now;
        now.tracked = tracked_.load();
        now.sent = sent_.load();
        now.dropped = dropped_.load();
        now.failed = failed_.load();
        return now;
    }

    void Dispatcher::Run(std::stop_token stop)
    {
        auto next_report = std::chrono::steady_clock::now() + config_.stats_interval;
        while (stop.stop_requested() == false)
        {
            std::vector<EventRecord> batch;
            {
                std::unique_lock lock(mutex_);
                cv_.wait_until(lock, stop, next_report, [this]
                {
                    return queue_.empty() == false;
                });
                if (stop.stop_requested())
                {
                    break;
                }
                if (queue_.empty() == false)
                {
                    batch = CutBatch();
                    in_flight_ = batch.size();
                }
            }
            if (batch.empty() == false)
            {
                SendWithRetry(batch, stop);
                {
                    std::lock_guard lock(mutex_);
                    in_flight_ = 0;
                }
                idle_cv_.notify_all();
            }
            if (config_.report_stats && std::chrono::steady_clock::now() >= next_report)
            {
                ReportStats();
                next_report += config_.stats_interval;
            }
        }
    }

    std::vector<EventRecord> Dispatcher::CutBatch()
    {
        std::vector<EventRecord> batch;
        const std::optional<std::string> player = queue_.front().player_id;
        while (queue_.empty() == false && batch.size() < config_.max_batch && queue_.front().player_id == player)
        {
            batch.push_back(std::move(queue_.front()));
            queue_.pop_front();
        }

        return batch;
    }

    void Dispatcher::SendWithRetry(const std::vector<EventRecord>& batch, std::stop_token stop)
    {
        const std::uint64_t count = batch.size();
        const std::optional<std::string>& player = batch.front().player_id;
        auto wait = config_.initial_backoff;
        bool refreshed = false;

        for (int attempt = 1; attempt <= config_.max_attempts; ++attempt)
        {
            std::optional<std::string> token;
            if (player.has_value())
            {
                token = identity_.AuthToken(*player);

                // That player has signed out, so nothing can authenticate these events
                if (token.has_value() == false)
                {
                    break;
                }
            }

            SendResult result = SendResult::Retry;
            try
            {
                result = transport_.Send(batch, token, config_.send_deadline);
            }
            catch (...)
            {
                result = SendResult::Retry;
            }

            if (result == SendResult::Ok)
            {
                sent_ += count;
                return;
            }
            if (result == SendResult::Rejected)
            {
                const std::optional<std::string> fresh = player.has_value() ? identity_.AuthToken(*player) : std::nullopt;
                if (refreshed == false && fresh.has_value() && token.has_value() && *fresh != *token)
                {
                    refreshed = true;
                    continue;
                }
                break;
            }
            if (attempt == config_.max_attempts || Backoff(wait, stop) == false)
            {
                break;
            }

            wait *= kBackoffMultiplier;
        }

        failed_ += count;
    }

    bool Dispatcher::Backoff(std::chrono::milliseconds wait, std::stop_token stop)
    {
        if (stop.stop_possible() == false)
        {
            std::this_thread::sleep_for(wait);
            return true;
        }

        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, stop, wait, []
        {
            return false;
        });

        return stop.stop_requested() == false;
    }
} // namespace tracking
