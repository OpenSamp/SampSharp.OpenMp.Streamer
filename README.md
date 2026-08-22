# SampSharp.OpenMp.Streamer

Managed C# bindings for the [Incognito streamer plugin](https://github.com/samp-incognito/samp-streamer-plugin)
on open.mp x64, for gamemodes running on the SampSharp open.mp host. Lets a C# gamemode
create far more objects, pickups, text labels, map icons, checkpoints, areas and actors than
the engine's own limits allow, with the plugin streaming them in and out around players.

## Architecture

open.mp loads three independent components; this repository provides the middle one plus
the C# bindings on top of it.

```
┌──────────────────────────────────────────────────────────────────────┐
│  C# gamemode                                                         │
│     IStreamerService, DynamicObject, DynamicPickup, ...              │
└──────────────────────────────────────────────────────────────────────┘
                               │   P/Invoke (LibraryImport("SampSharp.Streamer"))
                               ▼
┌──────────────────────────────────────────────────────────────────────┐
│  SampSharp.Streamer.dll  (this repo, native/)                        │
│     Streamer_Object_Create / Pickup / TextLabel / MapIcon / ...      │
│     queryExtension<IStreamerComponent>() at onInit                   │
└──────────────────────────────────────────────────────────────────────┘
                               │   direct C++ virtual calls (no AMX)
                               ▼
┌──────────────────────────────────────────────────────────────────────┐
│  streamer.dll  (Incognito streamer x64 port, our fork)               │
│     IStreamerComponent : IExtension                                  │
│     dispatches into Core::getData() / Grid / StreamerApi             │
└──────────────────────────────────────────────────────────────────────┘
```

Calls reach the plugin through its component interface, not through AMX — no Pawn round
trip on the hot path. The shim has **no link-time dependency on `SampSharp.dll`**; the
managed side reaches it by P/Invoke.

## Runtime dependencies

| Component                | Where from                                             |
|--------------------------|--------------------------------------------------------|
| `streamer.dll`/`.so`     | Our fork of `samp-streamer-plugin` with IStreamerComponent |
| `SampSharp.Streamer.dll` | Built from `native/` in this repository                 |
| `SampSharp.dll`          | `SampSharp/src/sampsharp-component/`                    |
| .NET 10 runtime          | System-wide                                             |

All three DLLs go in the server's `components/` directory.

If `streamer.dll` is not loaded at server start, every `Create*` returns a stub with
`Id = 0` and `IsAlive = false`, silently. Check `IStreamerService.IsAvailable` rather than
waiting for an exception that will not come.

## Wiring

```csharp
// ConfigureServices — before AddSystemsInAssembly
services.AddStreamer();

// ECS builder
builder.EnableStreamerEvents();
```

## Surface

- **Objects** — create, destroy, validate, move/stop, get and set position and rotation,
  attach to object / player / vehicle, set material colour and text, edit
- **Pickups**, **3D text labels**, **map icons**, **checkpoints**, **race checkpoints**,
  **areas**, **actors** — create, destroy, validate, plus the per-type operations each
  supports (label text and colour, actor animations, health and invulnerability, …)

Every wrapper type derives from `DynamicEntity`, so `Id` and `IsAlive` behave uniformly.

## Events

Handlers are ordinary ECS `[Event]` methods:

- **Stream in/out** — `OnDynamicObjectStreamIn` / `Out`, and the same pair for pickups,
  text labels, map icons and checkpoints
- **Objects** — `OnDynamicObjectMoved`, `OnPlayerEditDynamicObject`,
  `OnPlayerSelectDynamicObject`, `OnPlayerShootDynamicObject`
- **Areas and checkpoints** — `OnPlayerEnterDynamicArea` / `Leave`,
  `OnPlayerEnterDynamicCP` / `Leave`, `OnPlayerEnterDynamicRaceCP` / `Leave`
- **Pickups** — `OnPlayerPickUpDynamicPickup`

`OnPlayerShootDynamicObject` honours a `false` return as a veto.

## Telemetry and tuning

`IStreamerService` exposes what the plugin is actually costing you, per item type:
`StreamerPhaseStats` carries cumulative time, average microseconds per player-tick, and
stream-in/out counts since the last `ResetPhaseStats`. The average is the useful
single-value health metric; the time is wall-clock, not CPU.

The same interface tunes the plugin at runtime: `SetHysteresisFactor` to stop items
thrashing on a radius boundary, and `TrySetCoarseCellSize` / `TrySetCoarseCellDistance` for
the coarse grid. The `Try*` pair returns `false` when the running plugin build does not
support the knob, so a stock `streamer.dll` degrades instead of failing.

## Building

Two artifacts, built separately:

```bash
# managed bindings
dotnet build SampSharp.OpenMp.Streamer.csproj

# native shim
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Both need the SampSharp repository checked out alongside this one — the csproj references
`SampSharp.OpenMp.Core` and `SampSharp.OpenMp.Entities` by relative path, and CMake takes
the open.mp SDK from `SampSharp/external/sdk`. Either nesting works: SampSharp as a direct
sibling, or one level up (`src/SampSharp` with this repo under `src/submodules/`). Override
with `-DOMP_SDK_DIR=<path>` if your layout differs.

## License

Apache-2.0, same as the upstream streamer plugin.

---

Powered by [vs-rp.org](https://vs-rp.org)
