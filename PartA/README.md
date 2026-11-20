# Part A: Game Event Tracking

A C++ event tracker composed of: 

- A client SDK that game code calls
- A small ingestion service that checks each event and stores it

In this submission the service runs in the same program and "storing" prints one line per event.

The design discussion, with strengths, weaknesses, and decisions is in [DESIGN.md](DESIGN.md).

## Build and Tun

Requires CMake 3.20 or later and a C++20 compiler. There are no third-party dependencies.

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Then run the demo. Its location depends on the generator:

```
build/demo                  # Ninja, Makefiles
build\Release\demo.exe      # Visual Studio
```

Built and tested with MSVC 19.38 (Visual Studio 2022) only.

## What the Demo Shows

1. **Pass 1, every send succeeds.** A startup stats report, two events before sign-in (`CRASH`, `LOGIN_FAILED`), the three events for the test (`LOGIN`, `START_MISSION`, `PURCHASED_ITEM`), a mission with a nested squad list, and a shutdown stats report.
2. **Pass 2, the transport fails on purpose.** A reply is lost, then a request is dropped. The SDK retries, and the store refuses the three events it already has, so nothing is stored twice.
3. **A summary.** For each pass: events tracked, acknowledged, dropped, and failed, sends attempted, and duplicates refused. Then the events stored by type, anything the service rejected, and whether every tracked event was stored exactly once. The demo exits with a non-zero code if not.

<details>
<summary>My output</summary>

```
== Pass 1: every send succeeds ==
TRACKER_STATS v1 event_id=01a1fa5d-555a-7ca4-a2c8-99e2e2c06147 time_ms=1795201783130 received_ms=1795201783130 session=b56141c45293f6ac install=d40922c9-f2df-4a7f-a3a4-51d12cbea0f7 player=none authenticated=false fields={tracked: 0, sent: 0, dropped: 0, failed: 0}
CRASH v1 event_id=01a1fa5d-555c-79b0-9b4d-932fc337a196 time_ms=1795201783132 received_ms=1795201783132 session=b56141c45293f6ac install=d40922c9-f2df-4a7f-a3a4-51d12cbea0f7 player=none authenticated=false fields={crashed_session_id: "5d0c9b1e2a7f4e11"}
LOGIN_FAILED v1 event_id=01a1fa5d-555d-72fe-8768-df563d80fa14 time_ms=1795201783133 received_ms=1795201783133 session=b56141c45293f6ac install=d40922c9-f2df-4a7f-a3a4-51d12cbea0f7 player=none authenticated=false fields={platform: "Steam", reason: "NETWORK_ERROR"}
LOGIN v1 event_id=01a1fa5d-555e-718f-b740-785c47a4a3aa time_ms=1795201783134 received_ms=1795201783134 session=b56141c45293f6ac install=d40922c9-f2df-4a7f-a3a4-51d12cbea0f7 player=67f68ce387eb193fe6b5e0ca authenticated=true fields={platform: "Steam"}
START_MISSION v1 event_id=01a1fa5d-555f-780a-b0f9-5c2b5120be26 time_ms=1795201783135 received_ms=1795201783135 session=b56141c45293f6ac install=d40922c9-f2df-4a7f-a3a4-51d12cbea0f7 player=67f68ce387eb193fe6b5e0ca authenticated=true fields={mission_type: "DEFENSE", node: "SolNode26"}
PURCHASED_ITEM v1 event_id=01a1fa5d-5561-7ec6-9d68-da37a8d20506 time_ms=1795201783137 received_ms=1795201783137 session=b56141c45293f6ac install=d40922c9-f2df-4a7f-a3a4-51d12cbea0f7 player=67f68ce387eb193fe6b5e0ca authenticated=true fields={item: "Forma", quantity: 3}
START_MISSION v1 event_id=01a1fa5d-5562-737f-a30f-1a4b4ecb8c89 time_ms=1795201783138 received_ms=1795201783138 session=b56141c45293f6ac install=d40922c9-f2df-4a7f-a3a4-51d12cbea0f7 player=67f68ce387eb193fe6b5e0ca authenticated=true fields={mission_type: "SURVIVAL", node: "SolNode26", squad: [{player_id: "67f68ce587eb193fe6b5e1ed", join_method: "PARTY"}, {player_id: "67f68ce587eb193fe6b5e3b2", join_method: "MATCHMAKING"}]}
TRACKER_STATS v1 event_id=01a1fa5d-5564-782a-b4d5-f23240c85c48 time_ms=1795201783140 received_ms=1795201783140 session=b56141c45293f6ac install=d40922c9-f2df-4a7f-a3a4-51d12cbea0f7 player=67f68ce387eb193fe6b5e0ca authenticated=true fields={tracked: 7, sent: 7, dropped: 0, failed: 0}

== Pass 2: the transport loses a reply, then drops a request ==
LOGIN v1 event_id=01a1fa5d-5566-7417-bb9c-5b671735d4f7 time_ms=1795201783142 received_ms=1795201783143 session=b56141c45293f6ac install=d40922c9-f2df-4a7f-a3a4-51d12cbea0f7 player=67f68ce387eb193fe6b5e0ca authenticated=true fields={platform: "Steam"}
START_MISSION v1 event_id=01a1fa5d-5566-71de-8328-937e7fdf65a3 time_ms=1795201783142 received_ms=1795201783143 session=b56141c45293f6ac install=d40922c9-f2df-4a7f-a3a4-51d12cbea0f7 player=67f68ce387eb193fe6b5e0ca authenticated=true fields={mission_type: "DEFENSE", node: "SolNode26"}
PURCHASED_ITEM v1 event_id=01a1fa5d-5566-7ebe-b96a-93f9747b2e6f time_ms=1795201783142 received_ms=1795201783143 session=b56141c45293f6ac install=d40922c9-f2df-4a7f-a3a4-51d12cbea0f7 player=67f68ce387eb193fe6b5e0ca authenticated=true fields={item: "Forma", quantity: 3}

== Summary ==
Pass 1, every send succeeds
  events tracked ............. 8  (6 from the game, 2 stats reports from the SDK)
  acknowledged by the service  8
  dropped, queue full ........ 0
  failed, gave up ............ 0
  sends attempted ............ 8
  duplicates the store refused 0
Pass 2, the transport fails on purpose
  events tracked ............. 3
  acknowledged by the service  3
  dropped, queue full ........ 0
  failed, gave up ............ 0
  sends attempted ............ 3  (1 reply lost, 1 request dropped, 1 clean)
  duplicates the store refused 3
Events stored: 11
  CRASH 1, LOGIN 2, LOGIN_FAILED 1, PURCHASED_ITEM 2, START_MISSION 3, TRACKER_STATS 2
Rejected by the service: none
Result: every tracked event was stored exactly once.
```

</details>

## Using It

```cpp
Config config(identity, transport);
DataTracker::Initialize(config);

DataTracker::TrackLogin({.platform = Platform::Steam});
DataTracker::TrackStartMission({.mission_type = MissionType::Defense, .node = "SolNode26"});
DataTracker::TrackPurchasedItem({.item = "Forma", .quantity = 3});

DataTracker::Shutdown();
```

`DataTracker::Track(event)` accepts any event struct. Each named `Track` function is just a wrapper for this.

## Code Style

The layout rules are in `.clang-format`, which Visual Studio applies on format: braces on their own lines, braces on every `if`, `else`, `for`, and `while` body, one statement per line, and no statement broken across lines. By convention the code also writes `== false` in place of `!`, avoids single-character names except loop counters, and stores a call's result in a named local before passing it to another call. Every `switch` case body has its own braces. There are no type aliases, so the real type is written out wherever it is used.

These style rules, as well as the `Track` overloads, are my personal preference. 

## Layout

Each class and object named after it. Inside `sdk/` and `service/`, plain data types are in `data/` and the interfaces to outside systems are in `interfaces/`.

| Path | Contents |
|---|---|
| `sdk/data_tracker.h`, `sdk/tracker.h` | The facade game code calls, and the instance behind it. |
| `sdk/dispatch.*` | Bounded queue, worker thread, batching, retries, and stats reports. |
| `sdk/envelope.h`, `sdk/ids.*`, `sdk/id_parts.*` | The event envelope, the event id, and the helpers that id generators share. |
| `sdk/events/` | One file per event and per enum. These files are the schema. |
| `sdk/schema/` | How a struct describes itself: `TrackableEvent`, `FieldType`, `Required`, `RecordBuilder`, which builds the record, and `SchemaBuilder`, which builds the schema. |
| `sdk/data/` | Plain data: `Config`, `DispatcherConfig`, `Stats`, `Identity`, `EventRecord`, `SendResult`, and `FieldValue`, the value of one field of an event. |
| `sdk/interfaces/` | `IIdentityProvider`, which stands for the platform service that supplies the session, install, and player ids, and `ITransport`, which hides the network. |
| `service/ingestion_service.*` | Authenticate, validate, store. Its constructor registers the events it accepts. |
| `service/validator.*` | `Validator`, one generic checker driven by a generated schema. |
| `service/data/` | `AcceptedEvent`, `IngestResult`, and `InsertResult`. |
| `service/interfaces/` | `IAuthVerifier` and `IEventStore`, the service's two interfaces to outside systems. |
| `tests/` | 107 checks, no framework. |
| `main.cpp` | The demo. |

Each system that I made the assumption (dangerous, I know) would exist elsewhere has a stand-in in `standins/`. The demo and the tests use them. Nothing in `sdk/` or `service/` includes one, and they build as a separate library.

| Stand-in | Stands in for |
|---|---|
| `standins/stub_identity_provider.h` | The platform service that supplies identity. |
| `standins/in_memory_transport.h` | The HTTPS connection to the service. |
| `standins/flaky_transport.h`, `standins/fault.h` | A network that fails, so retries can be tested. |
| `standins/stub_auth_verifier.h` | The platform service's token check. |
| `standins/console_store.*` | MongoDB. It prints one plain line per stored event. |
