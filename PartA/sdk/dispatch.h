#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

#include "sdk/data/dispatcher_config.h"
#include "sdk/data/event_record.h"
#include "sdk/data/stats.h"
#include "sdk/interfaces/identity_provider.h"
#include "sdk/interfaces/transport.h"

namespace tracking
{
    // Moves records from the caller's thread to the transport
    class Dispatcher
    {
    public:
        Dispatcher(ITransport& transport, IIdentityProvider& identity, DispatcherConfig config = {});
        ~Dispatcher();

        Dispatcher(const Dispatcher&) = delete;
        Dispatcher& operator=(const Dispatcher&) = delete;

        void Enqueue(EventRecord record) noexcept;

        // Counts an event that could not be built, for example on allocation failure.
        void CountFailed() noexcept
        {
            ++failed_;
        }

        bool Flush(std::chrono::milliseconds timeout);

        // Any events still queued counts as failed.
        void Stop(std::chrono::milliseconds timeout);
        void Stop()
        {
            Stop(config_.shutdown_timeout);
        }

        void ReportStats() noexcept;

        Stats stats() const;

    private:
        void Run(std::stop_token stop);
        std::vector<EventRecord> CutBatch();
        void SendWithRetry(const std::vector<EventRecord>& batch, std::stop_token stop);
        bool Backoff(std::chrono::milliseconds wait, std::stop_token stop);

        ITransport& transport_;
        IIdentityProvider& identity_;
        const DispatcherConfig config_;

        mutable std::mutex mutex_;
        std::condition_variable_any cv_;
        std::condition_variable idle_cv_;
        std::deque<EventRecord> queue_;
        std::size_t in_flight_ = 0;
        std::mutex send_mutex_;

        std::atomic<bool> accepting_{true};
        std::atomic<bool> stopped_{false};
        std::atomic<std::uint64_t> tracked_{0};
        std::atomic<std::uint64_t> sent_{0};
        std::atomic<std::uint64_t> dropped_{0};
        std::atomic<std::uint64_t> failed_{0};

        std::jthread worker_;
    };
} // namespace tracking
