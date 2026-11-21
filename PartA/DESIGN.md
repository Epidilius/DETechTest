# Part A Design: Strengths, Weaknesses, and Decisions

## What was Built

A client SDK and an ingestion service.

```
game code                    SDK, caller's thread         SDK, worker thread          ingestion service
---------                    --------------------         ------------------          -----------------
DataTracker::Track(event) -> build record, stamp id,   -> batch by identity,       -> verify token
                             time, and identity           send, retry with            check against schema
                             enqueue (bounded)            backoff                     store, dropping repeats
```

Printing to console stands in for the upload. Here the service runs in the same program behind an in-memory transport, and the store prints one plain line per event where it would insert a document into MongoDB. 

## Where the Design Comes From

I based the tracker on the logger we use in Chrono CCG, which I also designed and built. It works in much the same way:

1. The logger takes an event.
2. It moves the event off the main thread.
3. It validates the event and processes the data. Validation is "Should this event should be logged", "Is the data present", and "Is the requester allowed to log this".
4. It sends the event data to our API.
5. The API ingests the event payload and validates it again, this time with only an auth check and data validation. It then writes the event to the database through a stored function.

The Chrono logger also loads its config from our database. It looks for a stored config on disk, generates and stores a default if there is none, and then checks the database for overrides. Our servers are transient, so they always start from the default, and being able to change their settings temporarily is valuable. That part is not built here, as I didn't think of it until I started typing out this section. Likewise, the ChronoLogger hooks into all uncaught exceptions and logs them, and I didn't think to implement the same feature here until just now.

## The Main Choices

**Events are typed structs.** `PurchasedItem{.item = "Forma", .quantity = 3}` is checked by the compiler. Data issues (wrong type, typo, missing required field, etc) does not build. Closed sets are enums, so `Steam` cannot become `steam`.

I took this approach for a couple of reasons. 

First and most important: I like being explicit. If something is going to fail, it should fail as early as possible, and compile time is as early as I can go. 

Second, because of the provided data. `logging.json` has a boolean is stored as the text `"true"` in all the documents that carry it, one map mixes ints and doubles, and `inventory` documents come in varying field combinations with nothing saying which to expect. This can occur when each call site is allowed to define what data (both fields and types) gets sent. Making a fixed data struct for each event prevents that.

I considered a data registry, named free-form data fields, and positional values. Working with a team, I would have asked for advice on which approach to take. I had a lot of freedom when making this, and considered a lot of trade offs. 

**One definition per event.** Each struct lists its members once, in `Describe`. This allows us to generate two things: the record the game sends and the schema the service validates against. They cannot drift, and there is no per-event validation.

**The service validates.** A game client is inherently untrustworthy: the API verifies the auth token, refuses an event whose player does not match the token, and validates every field against the schema.

**Database credentials stay off the client.** The game knows an endpoint. Only the service knows of the database. 

**Identity is not the caller's or SDK's job.** `Track()` takes no ids, and the SDK does not make them. It reads them from an existing platform service. Every event carries a per-launch `session_id` and a saved `install_id`, so events before sign-in, such as a failed login, can still be grouped. `player_id` is added once the player signs in, so we can connect these ids later. A batch holds a single identity, and its token is read when the batch is sent.

**There is one startup call, `DataTracker::Initialize`.** Some player information has to be present on every event, and I did not want each `Track` call to pass the same data points. Thus `Initialize` was created, and it is given the identity provider once. I am not sure what the best approach for this would be. Would the game have an existing event hook to attach to? Chrono, for example, fires events when the application starts and closes, and when the player authenticates. 

I judged it safe to rely on the team following this single instruction, but I would prefer something a little more automated. I also did not know how the tracker would be delivered to the team (part of the project? .dll? something else?), so a self-contained SDK with one startup point seemed like a safe approach.

**The SDK makes one id, the `event_id`.** It has to exist before the first send, so that a retry carries the same id and the store can refuse the repeat. A remote service could not supply it without a request per event.

**The store prints plain lines for now.** Each stored event is printed as one line: its envelope, then its own fields. Turning an event into a document that follows the provided collection is left to the real MongoDB store.

## Adding an Event

1. Write the struct in a new file in `sdk/events/`:

   ```cpp
   struct EmoteUsed
   {
       static constexpr std::string_view kName = "EMOTE_USED";
       static constexpr std::uint32_t kVersion = 1;

       Required<std::string> emote;
       std::optional<std::int32_t> count; // left out when unset

       static void Describe(auto& visit)
       {
           visit("emote", &EmoteUsed::emote);
           visit("count", &EmoteUsed::count);
       }
   };
   ```

2. Include that file in `service/ingestion_service.cpp`, and register the event in the `IngestionService` constructor: `Register<EmoteUsed>();`
3. Optionally add a wrapper to `DataTracker` named `TrackEmoteUsed`, which takes a `const EmoteUsed&` and forwards it to `Track`. It needs the same include in `sdk/data_tracker.h`.

Every member must be `Required<T>` or `std::optional<T>`. Anything else fails to compile. A member can be a `std::vector` or another described struct; `StartMission::squad` is an optional array of objects.

## Failure Handling

The game must never pay for telemetry, so failure is the tracker's to handle.

- `Track()` is `noexcept` and does no I/O. It builds a record, takes one short lock to enqueue it, and returns.
- The queue is bounded. When it is full the event is dropped and counted, so memory stays bounded however long the service is down.
- A failed send is retried with exponential backoff. A retry can deliver a batch twice, so each event has a UUIDv7 `event_id` and the store refuses ids it already has.
- `Shutdown()` flushes within a time limit and wakes the worker out of a backoff, so exit is not held up by a service that is down.
- The SDK reports its own counters as a `TRACKER_STATS` event at startup, every few minutes, and at shutdown. They are cumulative, so a lost report is covered by the next. A session with a startup report and no shutdown report ended in a crash or a kill.

`FlakyTransport` wraps the transport and fails on purpose. The demo's second pass and the tests use it, so retry, de-duplication, and loss counting are exercised and not only described.

## Strengths

- Mistakes are caught at the earliest point: by the compiler at the call site, then by the service before anything is stored.
- One struct defines an event for both the game and the service.
- The network is one interface, `ITransport`. Swapping the in-memory transport for HTTPS changes nothing else.
- The database is one interface, `IEventStore`. A different database needs only a new implementation of it.
- Loss is measured.
- No third-party dependencies, and the tests need no framework.

## Weaknesses

- **Delivery is best-effort, not at-least-once.** An event is lost if the queue is full, retries run out, the process crashes with events queued, or shutdown runs out of time. All but the crash are counted.
- **The queue is in memory.** A disk spool would cover crashes and long outages.
- **The service is simulated.** There is no wire format, and real timeouts, TLS, and compression are not exercised. The in-memory transport ignores the send deadline, so a transport that blocked forever would hold up shutdown.
- **A new event needs a build and a client patch.** There is no untyped call to fall back on. A schema shipped as data would allow that, at the cost of compile-time checks.
- **The service reads schemas from the game's event headers.** A production service would not share those headers, so the build would export each schema as a file.
- **Schemas come from types alone.** A rule such as "quantity is at least 1" is not generated.
- **Field names are free text inside `Describe`.** They are written once per event, but a typo there compiles and ships under the wrong name.
- **Cost per call.** Each call builds the struct and then a record, with a few heap allocations on the caller's thread. Data pooling would help reduce this.
- **Events before sign-in are unauthenticated.** A real service would have to rate-limit them.
- **Schema evolution is a version number.** There is no compatibility checking between versions.
- **`DataTracker` is global state.** Tests construct a `Tracker` directly.
- **One compiler.** Built and tested with MSVC 19.38 only.
- **It is both over- and under-engineered.** I wanted to impress, so I tried to maximize my use of the alloted time. As this list of weaknesses shows, it is also lacking many features despite that.
- **The naming convention is my own.** I would have preferred to follow an existing style. I like the convention I followed, but following existing style takes precedence.
- **The unnamed namespaces.** I understand that they remove external linkage, so only the one file can use what is inside, but in general I don't like them. That may be a holdover from working in C# recently. In practice, most of the data in these namespaces would come from a file or a database.
- **Config does not come from a database.** The settings in `DispatcherConfig` are defaults written in the code, which the caller can change.

## What was Verified

- Builds with `/W4 /WX` and no warnings, using the NMake generator. Each header also compiles on its own.
- 107 checks pass, and passed on 40 consecutive runs. They cover the record and schema generation, the validator, a lost reply, exhausted retries, a full queue, batching by identity, sign-out with events queued, what the service refuses, stats reports, tracking from four threads, and a shutdown during a backoff.
