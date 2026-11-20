#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>

#include "sdk/data/config.h"
#include "sdk/data/stats.h"
#include "sdk/dispatch.h"
#include "sdk/events/crash.h"
#include "sdk/events/login.h"
#include "sdk/events/login_failed.h"
#include "sdk/events/purchased_item.h"
#include "sdk/events/start_mission.h"
#include "sdk/schema/trackable_event.h"
#include "sdk/tracker.h"

namespace tracking
{

    class DataTracker
    {
    public:
        // Call once at startup. `identity` and `transport` must outlive Shutdown().
        static void Initialize(const Config& config)
        {
            auto fresh = std::make_unique<Runtime>(config);
            std::unique_lock lock(mutex_);
            runtime_.swap(fresh);
        }

        // Sends a final stats report and flushes what is queued, within the configured timeout
        static Stats Shutdown()
        {
            std::unique_ptr<Runtime> old;
            {
                std::unique_lock lock(mutex_);
                old.swap(runtime_);
            }

            if (old == nullptr)
            {
                return Stats();
            }
            
            old->dispatcher.Stop();
            return old->dispatcher.stats();
        }

        template <TrackableEvent E>
        static void Track(const E& event) noexcept
        {
            try
            {
                std::shared_lock lock(mutex_);
                if (runtime_ != nullptr)
                {
                    runtime_->tracker.Track(event);
                }
                else
                {
                    ++ignored_; // before Initialize or after Shutdown
                }
            }
            catch (...)
            {
                ++ignored_;
            }
        }

        // One wrapper per event type, named Track plus the struct.
        static void TrackLogin(const Login& event) noexcept
        {
            Track(event);
        }
        static void TrackStartMission(const StartMission& event) noexcept
        {
            Track(event);
        }
        static void TrackPurchasedItem(const PurchasedItem& event) noexcept
        {
            Track(event);
        }
        static void TrackCrash(const Crash& event) noexcept
        {
            Track(event);
        }
        static void TrackLoginFailed(const LoginFailed& event) noexcept
        {
            Track(event);
        }

        // States since Initialize
        static Stats GetStats()
        {
            std::shared_lock lock(mutex_);
            return runtime_ != nullptr ? runtime_->dispatcher.stats() : Stats();
        }

        // Waits for the queue to drain. Mainly for the demo and tests.
        static bool Flush(std::chrono::milliseconds timeout)
        {
            std::shared_lock lock(mutex_);
            return runtime_ != nullptr ? runtime_->dispatcher.Flush(timeout) : true;
        }

        // Calls made while the tracker was not running.
        static std::uint64_t IgnoredCalls()
        {
            return ignored_.load();
        }

    private:
        struct Runtime
        {
            explicit Runtime(const Config& config) : dispatcher(config.transport, config.identity, config.dispatcher), tracker(config.identity, dispatcher)
            {
            }
            Dispatcher dispatcher;
            Tracker tracker;
        };

        static inline std::shared_mutex mutex_;
        static inline std::unique_ptr<Runtime> runtime_;
        static inline std::atomic<std::uint64_t> ignored_{0};
    };
} // namespace tracking
