using System;
using System.Numerics;
using System.Runtime.InteropServices;
using SampSharp.Entities;
using SampSharp.Entities.SAMP;
using SampSharp.OpenMp.Core;
using SampSharp.OpenMp.Core.Api;
using SampSharp.Streamer.Entities.Interop;

namespace SampSharp.Streamer.Entities;

/// <summary>
/// Мост streamer-событий в ECS. Регистрирует UnmanagedCallersOnly-методы как
/// callback'и в SampSharp.dll, которые синхронно дёргаются на open.mp tick'е
/// из streamer.dll. На каждом event вызывается <c>IEventDispatcher.Invoke(...)</c>,
/// который маршалит вызов в <c>[Event]</c>-методы ECS-систем.
///
/// Event-names 1-в-1 под legacy SampSharp.Streamer (OnPlayerPickUpDynamicPickup и т.д.).
/// </summary>
internal sealed class StreamerEventSystem : ISystem
{
    private static IEventDispatcher? _dispatcher;
    private static IOmpEntityProvider? _entityProvider;
    private static bool _registered;
    private static readonly object _sync = new();

    public StreamerEventSystem(IEventDispatcher dispatcher, IOmpEntityProvider entityProvider,
        SampSharpEnvironment environment)
    {
        lock (_sync)
        {
            _dispatcher = dispatcher;
            _entityProvider = entityProvider;
            SampSharpEnvironmentAccessor.Bind(environment);
            if (_registered) return;
            RegisterCallbacks();
            _registered = true;
        }
    }

    private static unsafe void RegisterCallbacks()
    {
        StreamerInterop.Streamer_SetCallback_PickUpDynamicPickup(&OnPickUp);
        StreamerInterop.Streamer_SetCallback_EnterDynamicCP(&OnEnterCp);
        StreamerInterop.Streamer_SetCallback_LeaveDynamicCP(&OnLeaveCp);
        StreamerInterop.Streamer_SetCallback_EnterDynamicRaceCP(&OnEnterRaceCp);
        StreamerInterop.Streamer_SetCallback_LeaveDynamicRaceCP(&OnLeaveRaceCp);
        StreamerInterop.Streamer_SetCallback_EnterDynamicArea(&OnEnterArea);
        StreamerInterop.Streamer_SetCallback_LeaveDynamicArea(&OnLeaveArea);
        StreamerInterop.Streamer_SetCallback_DynamicObjectMoved(&OnObjectMoved);
        StreamerInterop.Streamer_SetCallback_DynamicObjectStreamIn(&OnObjStreamIn);
        StreamerInterop.Streamer_SetCallback_DynamicObjectStreamOut(&OnObjStreamOut);
        StreamerInterop.Streamer_SetCallback_DynamicPickupStreamIn(&OnPkpStreamIn);
        StreamerInterop.Streamer_SetCallback_DynamicPickupStreamOut(&OnPkpStreamOut);
        StreamerInterop.Streamer_SetCallback_DynamicTextLabelStreamIn(&OnLblStreamIn);
        StreamerInterop.Streamer_SetCallback_DynamicTextLabelStreamOut(&OnLblStreamOut);
        StreamerInterop.Streamer_SetCallback_DynamicCheckpointStreamIn(&OnCpStreamIn);
        StreamerInterop.Streamer_SetCallback_DynamicCheckpointStreamOut(&OnCpStreamOut);
        StreamerInterop.Streamer_SetCallback_DynamicMapIconStreamIn(&OnIconStreamIn);
        StreamerInterop.Streamer_SetCallback_DynamicMapIconStreamOut(&OnIconStreamOut);
        StreamerInterop.Streamer_SetCallback_PlayerEditDynamicObject(&OnEditObject);
        StreamerInterop.Streamer_SetCallback_PlayerSelectDynamicObject(&OnSelectObject);
        StreamerInterop.Streamer_SetCallback_PlayerShootDynamicObject(&OnShootObject);
    }

    private static EntityId PlayerEntity(int playerId)
    {
        if (_entityProvider is null) return default;
        try
        {
            var pool = SampSharpEnvironmentAccessor.TryGetPlayerPool();
            if (pool is null) return default;
            var player = pool.Value.Get(playerId);
            if (!player.HasValue) return default;
            return _entityProvider.GetEntity(player);
        }
        catch { return default; }
    }

    private static DynamicObject WrapObject(int id) => new(id, 0, Vector3.Zero, Vector3.Zero);
    private static DynamicPickup WrapPickup(int id) => new(id, 0, default, Vector3.Zero);
    private static DynamicTextLabel WrapLabel(int id) => new(id, string.Empty, default, Vector3.Zero, 0f);
    private static DynamicMapIcon WrapIcon(int id) => new(id, Vector3.Zero, default, default);
    private static DynamicCheckpoint WrapCheckpoint(int id) => new(id, Vector3.Zero, 0f);

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static void OnPickUp(int p, int id) =>
        _dispatcher?.Invoke("OnPlayerPickUpDynamicPickup", PlayerEntity(p), WrapPickup(id));

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static void OnEnterCp(int p, int id) =>
        _dispatcher?.Invoke("OnPlayerEnterDynamicCP", PlayerEntity(p), WrapCheckpoint(id));

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static void OnLeaveCp(int p, int id) =>
        _dispatcher?.Invoke("OnPlayerLeaveDynamicCP", PlayerEntity(p), WrapCheckpoint(id));

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static void OnEnterRaceCp(int p, int id) =>
        _dispatcher?.Invoke("OnPlayerEnterDynamicRaceCP", PlayerEntity(p), id);

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static void OnLeaveRaceCp(int p, int id) =>
        _dispatcher?.Invoke("OnPlayerLeaveDynamicRaceCP", PlayerEntity(p), id);

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static void OnEnterArea(int p, int id) =>
        _dispatcher?.Invoke("OnPlayerEnterDynamicArea", PlayerEntity(p), id);

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static void OnLeaveArea(int p, int id) =>
        _dispatcher?.Invoke("OnPlayerLeaveDynamicArea", PlayerEntity(p), id);

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static void OnObjectMoved(int id) =>
        _dispatcher?.Invoke("OnDynamicObjectMoved", WrapObject(id));

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static void OnObjStreamIn(int id, int f) =>
        _dispatcher?.Invoke("OnDynamicObjectStreamIn", WrapObject(id), PlayerEntity(f));

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static void OnObjStreamOut(int id, int f) =>
        _dispatcher?.Invoke("OnDynamicObjectStreamOut", WrapObject(id), PlayerEntity(f));

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static void OnPkpStreamIn(int id, int f) =>
        _dispatcher?.Invoke("OnDynamicPickupStreamIn", WrapPickup(id), PlayerEntity(f));

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static void OnPkpStreamOut(int id, int f) =>
        _dispatcher?.Invoke("OnDynamicPickupStreamOut", WrapPickup(id), PlayerEntity(f));

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static void OnLblStreamIn(int id, int f) =>
        _dispatcher?.Invoke("OnDynamicTextLabelStreamIn", WrapLabel(id), PlayerEntity(f));

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static void OnLblStreamOut(int id, int f) =>
        _dispatcher?.Invoke("OnDynamicTextLabelStreamOut", WrapLabel(id), PlayerEntity(f));

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static void OnCpStreamIn(int id, int f) =>
        _dispatcher?.Invoke("OnDynamicCheckpointStreamIn", WrapCheckpoint(id), PlayerEntity(f));

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static void OnCpStreamOut(int id, int f) =>
        _dispatcher?.Invoke("OnDynamicCheckpointStreamOut", WrapCheckpoint(id), PlayerEntity(f));

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static void OnIconStreamIn(int id, int f) =>
        _dispatcher?.Invoke("OnDynamicMapIconStreamIn", WrapIcon(id), PlayerEntity(f));

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static void OnIconStreamOut(int id, int f) =>
        _dispatcher?.Invoke("OnDynamicMapIconStreamOut", WrapIcon(id), PlayerEntity(f));

    /// <summary>Return: 0=Continue, 1=Consume (stop propagation), 2=Veto.</summary>
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static int OnEditObject(int p, int o, int r, float x, float y, float z,
        float rx, float ry, float rz)
    {
        var res = _dispatcher?.InvokeAs<object?>("OnPlayerEditDynamicObject", null,
            PlayerEntity(p), WrapObject(o), (EditObjectResponse)r,
            new Vector3(x, y, z), new Vector3(rx, ry, rz));
        return res is true ? 1 : 0;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static int OnSelectObject(int p, int o, int m, float x, float y, float z)
    {
        var res = _dispatcher?.InvokeAs<object?>("OnPlayerSelectDynamicObject", null,
            PlayerEntity(p), WrapObject(o), m, new Vector3(x, y, z));
        return res is true ? 1 : 0;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static int OnShootObject(int p, int w, int o, float x, float y, float z)
    {
        var res = _dispatcher?.InvokeAs<object?>("OnPlayerShootDynamicObject", null,
            PlayerEntity(p), w, WrapObject(o), new Vector3(x, y, z));
        return res is false ? 2 /* veto */ : 0 /* continue */;
    }
}

/// <summary>Static holder для IPlayerPool — UnmanagedCallersOnly-методы не могут иметь this.</summary>
internal static class SampSharpEnvironmentAccessor
{
    private static IPlayerPool? _pool;
    public static void Bind(SampSharpEnvironment env) => _pool = env?.Core.GetPlayers();
    public static IPlayerPool? TryGetPlayerPool() => _pool;
}
