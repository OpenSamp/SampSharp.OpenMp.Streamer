# SampSharp.OpenMp.Streamer

Managed (.NET 9) C# bindings for [Incognito streamer plugin](https://github.com/samp-incognito/samp-streamer-plugin)
running on open.mp x64, via the SampSharp open.mp x64 host.

## Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│  C# gamemode                                                         │
│     uses IStreamerService, DynamicObject, DynamicPickup, ...         │
└──────────────────────────────────────────────────────────────────────┘
                               │   P/Invoke (LibraryImport("SampSharp"))
                               ▼
┌──────────────────────────────────────────────────────────────────────┐
│  SampSharp.dll (open.mp component)                                   │
│     Streamer_Object_Create / Pickup / TextLabel / MapIcon / ...      │
│     Queries queryExtension<IStreamerComponent>() at onInit           │
└──────────────────────────────────────────────────────────────────────┘
                               │   direct C++ virtual calls (no AMX)
                               ▼
┌──────────────────────────────────────────────────────────────────────┐
│  streamer.dll (Incognito streamer x64 port, our fork)                │
│     IStreamerComponent : IExtension                                  │
│     Methods dispatch into Core::getData() / Grid / StreamerApi       │
└──────────────────────────────────────────────────────────────────────┘
```

Both `SampSharp.dll` and `streamer.dll` are open.mp components loaded from
`components/`; this package is only the C# bindings.

## Runtime dependencies

| Component              | Where from                                                       |
|------------------------|------------------------------------------------------------------|
| `SampSharp.dll`        | `SampSharp/src/sampsharp-component/`                             |
| `streamer.dll`         | Our fork of `samp-streamer-plugin` with IStreamerComponent       |
| `.NET 10` runtime      | System-wide                                                      |

If `streamer.dll` isn't loaded by open.mp at server start, all Create*
methods return stub objects with `Id=0, IsAlive=false` and log nothing —
`IStreamerService.IsAvailable` returns `false`.

## Surface (MVP)

- **Objects**: create / destroy / validate / move / stop / get+set pos/rot /
  attach to object/player/vehicle / set material (color + text) / edit
- **Pickups**: create / destroy / validate
- **3D Text Labels**: create / destroy / update text+color / validate
- **Map Icons**: create / destroy / validate
- **Checkpoints**: create / destroy / validate

Not in MVP: events (stream-in/out, pickup-pickup, enter/leave checkpoint/area,
select/edit/shoot dynamic object), `DynamicArea`, `DynamicRaceCheckpoint`,
`DynamicActor`, multi-world/interior/player filter overloads, per-item extras.

## License

Apache-2.0, same as upstream streamer plugin.
