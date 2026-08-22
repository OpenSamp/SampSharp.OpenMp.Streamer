// SampSharp.Streamer — open.mp component that bridges Incognito streamer (exposed as
// IStreamerComponent IExtension) to managed .NET (via C-exports + function-pointer callbacks).
//
// Architecture:
//   open.mp loads 3 components independently:
//     - streamer.dll        (provides IStreamerComponent extension)
//     - SampSharp.dll       (hosts the .NET runtime + gamemode)
//     - SampSharp.Streamer.dll  (THIS) — pure C-API shim
//
//   At onInit this component queryComponent's streamer and registers a C++ event handler.
//   Managed side (SampSharp.OpenMp.Streamer.csproj) P/Invokes into this DLL's exports.
//
//   This component has no dependency on SampSharp.dll — entirely decoupled.

#include <sdk.hpp>

#include "streamer-api.hpp"

namespace
{
    // Component identity. Different from both streamer.dll and SampSharp.dll UIDs.
    constexpr UID kSampSharpStreamerUID = UID(0x53735374726d7253ULL); // "SsStrmrS"

    IStreamerComponent* g_streamer = nullptr;

    // ---- Managed callback function pointers ----
    // На Linux x64 calling convention один (System V), `__cdecl` расшифровывается
    // в `__attribute__((__cdecl__))` в позиции, которую GCC отказывается парсить
    // в using-алиасе → дальше ломается весь TU. На Win/x86 cdecl нужен, но мы там
    // через MSVC, который ест keyword-форму без аттрибута.
    using FnPlayerInt = void (*)(int, int);
    using FnIntInt    = void (*)(int, int);
    using FnInt       = void (*)(int);
    using FnEdit      = int  (*)(int, int, int, float, float, float, float, float, float);
    using FnSelect    = int  (*)(int, int, int, float, float, float);
    using FnShoot     = int  (*)(int, int, int, float, float, float);

    FnPlayerInt cb_pickup        = nullptr;
    FnPlayerInt cb_enterCp       = nullptr;
    FnPlayerInt cb_leaveCp       = nullptr;
    FnPlayerInt cb_enterRaceCp   = nullptr;
    FnPlayerInt cb_leaveRaceCp   = nullptr;
    FnPlayerInt cb_enterArea     = nullptr;
    FnPlayerInt cb_leaveArea     = nullptr;
    FnInt       cb_objectMoved   = nullptr;
    FnIntInt    cb_objStreamIn   = nullptr;
    FnIntInt    cb_objStreamOut  = nullptr;
    FnIntInt    cb_pkpStreamIn   = nullptr;
    FnIntInt    cb_pkpStreamOut  = nullptr;
    FnIntInt    cb_lblStreamIn   = nullptr;
    FnIntInt    cb_lblStreamOut  = nullptr;
    FnIntInt    cb_cpStreamIn    = nullptr;
    FnIntInt    cb_cpStreamOut   = nullptr;
    FnIntInt    cb_iconStreamIn  = nullptr;
    FnIntInt    cb_iconStreamOut = nullptr;
    FnEdit      cb_editObj       = nullptr;
    FnSelect    cb_selectObj     = nullptr;
    FnShoot     cb_shootObj      = nullptr;

    class Handler : public IStreamerEventHandler
    {
    public:
        void onPlayerPickUpDynamicPickup(int p, int id) override       { if (cb_pickup) cb_pickup(p, id); }
        void onPlayerEnterDynamicCheckpoint(int p, int id) override    { if (cb_enterCp) cb_enterCp(p, id); }
        void onPlayerLeaveDynamicCheckpoint(int p, int id) override    { if (cb_leaveCp) cb_leaveCp(p, id); }
        void onPlayerEnterDynamicRaceCheckpoint(int p, int id) override{ if (cb_enterRaceCp) cb_enterRaceCp(p, id); }
        void onPlayerLeaveDynamicRaceCheckpoint(int p, int id) override{ if (cb_leaveRaceCp) cb_leaveRaceCp(p, id); }
        void onPlayerEnterDynamicArea(int p, int id) override          { if (cb_enterArea) cb_enterArea(p, id); }
        void onPlayerLeaveDynamicArea(int p, int id) override          { if (cb_leaveArea) cb_leaveArea(p, id); }
        void onDynamicObjectMoved(int id) override                     { if (cb_objectMoved) cb_objectMoved(id); }
        void onDynamicObjectStreamIn(int id, int f) override           { if (cb_objStreamIn)  cb_objStreamIn(id, f); }
        void onDynamicObjectStreamOut(int id, int f) override          { if (cb_objStreamOut) cb_objStreamOut(id, f); }
        void onDynamicPickupStreamIn(int id, int f) override           { if (cb_pkpStreamIn)  cb_pkpStreamIn(id, f); }
        void onDynamicPickupStreamOut(int id, int f) override          { if (cb_pkpStreamOut) cb_pkpStreamOut(id, f); }
        void onDynamicTextLabelStreamIn(int id, int f) override        { if (cb_lblStreamIn)  cb_lblStreamIn(id, f); }
        void onDynamicTextLabelStreamOut(int id, int f) override       { if (cb_lblStreamOut) cb_lblStreamOut(id, f); }
        void onDynamicCheckpointStreamIn(int id, int f) override       { if (cb_cpStreamIn)   cb_cpStreamIn(id, f); }
        void onDynamicCheckpointStreamOut(int id, int f) override      { if (cb_cpStreamOut)  cb_cpStreamOut(id, f); }
        void onDynamicMapIconStreamIn(int id, int f) override          { if (cb_iconStreamIn) cb_iconStreamIn(id, f); }
        void onDynamicMapIconStreamOut(int id, int f) override         { if (cb_iconStreamOut)cb_iconStreamOut(id, f); }

        StreamerHandlerResult onPlayerEditDynamicObject(int p, int o, int r,
            float x, float y, float z, float rx, float ry, float rz) override
        {
            if (!cb_editObj) return StreamerHandlerResult::Continue;
            return static_cast<StreamerHandlerResult>(cb_editObj(p, o, r, x, y, z, rx, ry, rz));
        }
        StreamerHandlerResult onPlayerSelectDynamicObject(int p, int o, int m,
            float x, float y, float z) override
        {
            if (!cb_selectObj) return StreamerHandlerResult::Continue;
            return static_cast<StreamerHandlerResult>(cb_selectObj(p, o, m, x, y, z));
        }
        StreamerHandlerResult onPlayerShootDynamicObject(int p, int w, int o,
            float x, float y, float z) override
        {
            if (!cb_shootObj) return StreamerHandlerResult::Continue;
            return static_cast<StreamerHandlerResult>(cb_shootObj(p, w, o, x, y, z));
        }
    };

    Handler g_handler;
    bool g_handlerRegistered = false;

    // ---- Component ----
    class SampSharpStreamerComponent final : public IComponent
    {
    public:
        PROVIDE_UID(kSampSharpStreamerUID)

        StringView componentName() const override { return "SampSharp.Streamer"; }
        SemanticVersion componentVersion() const override { return SemanticVersion(1, 0, 0, 0); }

        void onLoad(ICore* c) override { core_ = c; }

        void onInit(IComponentList* components) override
        {
            if (!components) return;
            IComponent* streamerComp = components->queryComponent(kStreamerComponentUID);
            if (!streamerComp)
            {
                if (core_) core_->logLn(LogLevel::Warning,
                    "SampSharp.Streamer: streamer.dll not loaded; all Streamer_* calls will be no-op");
                return;
            }
            g_streamer = queryExtension<IStreamerComponent>(streamerComp);
            if (!g_streamer)
            {
                if (core_) core_->logLn(LogLevel::Warning,
                    "SampSharp.Streamer: streamer.dll loaded but IStreamerComponent extension missing; use the open.mp x64 streamer fork");
                return;
            }
            g_streamer->addEventHandler(&g_handler);
            g_handlerRegistered = true;
            if (core_) core_->printLn("SampSharp.Streamer: bound to streamer.dll IStreamerComponent extension");
        }

        void free() override
        {
            if (g_streamer && g_handlerRegistered)
            {
                g_streamer->removeEventHandler(&g_handler);
                g_handlerRegistered = false;
            }
            g_streamer = nullptr;
            delete this;
        }

        void reset() override {}

    private:
        ICore* core_ = nullptr;
    };

    SampSharpStreamerComponent* g_componentInstance = nullptr;
}

COMPONENT_ENTRY_POINT()
{
    if (!g_componentInstance) g_componentInstance = new SampSharpStreamerComponent();
    return g_componentInstance;
}

// ============================================================================
// Objects
// ============================================================================

extern "C" SDK_EXPORT int __CDECL Streamer_Object_Create(int modelId,
    float posX, float posY, float posZ, float rotX, float rotY, float rotZ,
    int worldId, int interiorId, int playerId,
    float streamDistance, float drawDistance, int areaId, int priority)
{
    if (!g_streamer) return 0;
    return g_streamer->createObject(modelId, posX, posY, posZ, rotX, rotY, rotZ,
        worldId, interiorId, playerId, streamDistance, drawDistance, areaId, priority);
}

extern "C" SDK_EXPORT bool __CDECL Streamer_Object_Destroy(int objectId)
{ return g_streamer && g_streamer->destroyObject(objectId); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Object_IsValid(int objectId)
{ return g_streamer && g_streamer->isValidObject(objectId); }
extern "C" SDK_EXPORT int __CDECL Streamer_Object_Move(int objectId,
    float tx, float ty, float tz, float speed, float rx, float ry, float rz)
{ return g_streamer ? g_streamer->moveObject(objectId, tx, ty, tz, speed, rx, ry, rz) : 0; }
extern "C" SDK_EXPORT bool __CDECL Streamer_Object_Stop(int objectId)
{ return g_streamer && g_streamer->stopObject(objectId); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Object_IsMoving(int objectId)
{ return g_streamer && g_streamer->isObjectMoving(objectId); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Object_GetPos(int objectId, float* x, float* y, float* z)
{ return g_streamer && x && y && z && g_streamer->getObjectPos(objectId, *x, *y, *z); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Object_SetPos(int objectId, float x, float y, float z)
{ return g_streamer && g_streamer->setObjectPos(objectId, x, y, z); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Object_GetRot(int objectId, float* x, float* y, float* z)
{ return g_streamer && x && y && z && g_streamer->getObjectRot(objectId, *x, *y, *z); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Object_SetRot(int objectId, float x, float y, float z)
{ return g_streamer && g_streamer->setObjectRot(objectId, x, y, z); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Object_AttachToObject(int o, int p,
    float ox, float oy, float oz, float rx, float ry, float rz, bool syncRotation)
{ return g_streamer && g_streamer->attachObjectToObject(o, p, ox, oy, oz, rx, ry, rz, syncRotation); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Object_AttachToPlayer(int o, int p,
    float ox, float oy, float oz, float rx, float ry, float rz)
{ return g_streamer && g_streamer->attachObjectToPlayer(o, p, ox, oy, oz, rx, ry, rz); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Object_AttachToVehicle(int o, int v,
    float ox, float oy, float oz, float rx, float ry, float rz)
{ return g_streamer && g_streamer->attachObjectToVehicle(o, v, ox, oy, oz, rx, ry, rz); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Object_SetMaterial(int o, int idx, int m,
    const char* txd, const char* tex, uint32_t color)
{ return g_streamer && g_streamer->setObjectMaterial(o, idx, m, txd, tex, color); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Object_SetMaterialText(int o, int idx,
    const char* text, int size, const char* font, int fs, bool bold,
    uint32_t fc, uint32_t bc, int align)
{ return g_streamer && g_streamer->setObjectMaterialText(o, idx, text, size, font, fs, bold, fc, bc, align); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Object_Edit(int p, int o)
{ return g_streamer && g_streamer->editObject(p, o); }

// ============================================================================
// Pickups / TextLabels / MapIcons / Checkpoints
// ============================================================================

extern "C" SDK_EXPORT int __CDECL Streamer_Pickup_Create(int modelId, int type,
    float x, float y, float z, int w, int i, int p, float sd, int a, int pr)
{ return g_streamer ? g_streamer->createPickup(modelId, type, x, y, z, w, i, p, sd, a, pr) : 0; }
extern "C" SDK_EXPORT bool __CDECL Streamer_Pickup_Destroy(int id) { return g_streamer && g_streamer->destroyPickup(id); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Pickup_IsValid(int id) { return g_streamer && g_streamer->isValidPickup(id); }

extern "C" SDK_EXPORT int __CDECL Streamer_TextLabel_Create(const char* text, uint32_t color,
    float x, float y, float z, float dd,
    int ap, int av, bool los,
    int w, int i, int p, float sd, int a, int pr)
{ return g_streamer ? g_streamer->createTextLabel(text, color, x, y, z, dd, ap, av, los, w, i, p, sd, a, pr) : 0; }
extern "C" SDK_EXPORT bool __CDECL Streamer_TextLabel_Destroy(int id) { return g_streamer && g_streamer->destroyTextLabel(id); }
extern "C" SDK_EXPORT bool __CDECL Streamer_TextLabel_Update(int id, uint32_t color, const char* text)
{ return g_streamer && g_streamer->updateTextLabelText(id, color, text); }
extern "C" SDK_EXPORT bool __CDECL Streamer_TextLabel_IsValid(int id) { return g_streamer && g_streamer->isValidTextLabel(id); }

extern "C" SDK_EXPORT int __CDECL Streamer_MapIcon_Create(float x, float y, float z,
    int type, uint32_t color, int w, int i, int p, float sd, int style, int a, int pr)
{ return g_streamer ? g_streamer->createMapIcon(x, y, z, type, color, w, i, p, sd, style, a, pr) : 0; }
extern "C" SDK_EXPORT bool __CDECL Streamer_MapIcon_Destroy(int id) { return g_streamer && g_streamer->destroyMapIcon(id); }
extern "C" SDK_EXPORT bool __CDECL Streamer_MapIcon_IsValid(int id) { return g_streamer && g_streamer->isValidMapIcon(id); }

extern "C" SDK_EXPORT int __CDECL Streamer_Checkpoint_Create(float x, float y, float z, float size,
    int w, int i, int p, float sd, int a, int pr)
{ return g_streamer ? g_streamer->createCheckpoint(x, y, z, size, w, i, p, sd, a, pr) : 0; }
extern "C" SDK_EXPORT bool __CDECL Streamer_Checkpoint_Destroy(int id) { return g_streamer && g_streamer->destroyCheckpoint(id); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Checkpoint_IsValid(int id) { return g_streamer && g_streamer->isValidCheckpoint(id); }

extern "C" SDK_EXPORT int __CDECL Streamer_Actor_Create(int modelId,
    float x, float y, float z, float rotation,
    bool invulnerable, float health, float streamDistance,
    int w, int i, int p, int a, int pr)
{ return g_streamer ? g_streamer->createActor(modelId, x, y, z, rotation, invulnerable, health,
    streamDistance, w, i, p, a, pr) : 0; }
extern "C" SDK_EXPORT bool __CDECL Streamer_Actor_Destroy(int id) { return g_streamer && g_streamer->destroyActor(id); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Actor_IsValid(int id) { return g_streamer && g_streamer->isValidActor(id); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Actor_ApplyAnimation(int id, const char* lib, const char* name,
    float delta, bool loop, bool lockX, bool lockY, bool freeze, int timeMs)
{ return g_streamer && g_streamer->applyActorAnimation(id, lib, name, delta, loop, lockX, lockY, freeze, timeMs); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Actor_ClearAnimations(int id)
{ return g_streamer && g_streamer->clearActorAnimations(id); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Actor_GetPos(int id, float* x, float* y, float* z)
{ return g_streamer && g_streamer->getActorPos(id, *x, *y, *z); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Actor_SetPos(int id, float x, float y, float z)
{ return g_streamer && g_streamer->setActorPos(id, x, y, z); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Actor_GetFacingAngle(int id, float* angle)
{ return g_streamer && g_streamer->getActorFacingAngle(id, *angle); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Actor_SetFacingAngle(int id, float angle)
{ return g_streamer && g_streamer->setActorFacingAngle(id, angle); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Actor_GetHealth(int id, float* health)
{ return g_streamer && g_streamer->getActorHealth(id, *health); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Actor_SetHealth(int id, float health)
{ return g_streamer && g_streamer->setActorHealth(id, health); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Actor_IsInvulnerable(int id)
{ return g_streamer && g_streamer->isActorInvulnerable(id); }
extern "C" SDK_EXPORT bool __CDECL Streamer_Actor_SetInvulnerable(int id, bool inv)
{ return g_streamer && g_streamer->setActorInvulnerable(id, inv); }
extern "C" SDK_EXPORT int __CDECL Streamer_Actor_GetVirtualWorld(int id)
{ return g_streamer ? g_streamer->getActorVirtualWorld(id) : 0; }
extern "C" SDK_EXPORT bool __CDECL Streamer_Actor_SetVirtualWorld(int id, int w)
{ return g_streamer && g_streamer->setActorVirtualWorld(id, w); }

// ============================================================================
// Introspection + event callback registration
// ============================================================================

extern "C" SDK_EXPORT bool __CDECL Streamer_IsAvailable() { return g_streamer != nullptr; }

// ============================================================================
// Telemetry (VS:RP fork) — per-phase timing + stream-in/out counters.
// Returns 0 when streamer.dll is missing. `type` is one of STREAMER_TYPE_*.
// ============================================================================

extern "C" SDK_EXPORT uint64_t __CDECL Streamer_GetPhaseTimeNs(int type)
{ return g_streamer ? g_streamer->getPhaseTimeNs(type) : 0; }
extern "C" SDK_EXPORT uint64_t __CDECL Streamer_GetPhaseAvgUs(int type)
{ return g_streamer ? g_streamer->getPhaseAvgUs(type) : 0; }
extern "C" SDK_EXPORT uint64_t __CDECL Streamer_GetPhaseTickCount()
{ return g_streamer ? g_streamer->getPhaseTickCount() : 0; }
extern "C" SDK_EXPORT uint64_t __CDECL Streamer_GetPhaseStreamInCount(int type)
{ return g_streamer ? g_streamer->getPhaseStreamInCount(type) : 0; }
extern "C" SDK_EXPORT uint64_t __CDECL Streamer_GetPhaseStreamOutCount(int type)
{ return g_streamer ? g_streamer->getPhaseStreamOutCount(type) : 0; }
extern "C" SDK_EXPORT void __CDECL Streamer_ResetPhaseStats()
{ if (g_streamer) g_streamer->resetPhaseStats(); }

// ============================================================================
// Anti-flicker hysteresis (VS:RP fork).
// ============================================================================

extern "C" SDK_EXPORT float __CDECL Streamer_GetHysteresisFactor(int type)
{ return g_streamer ? g_streamer->getHysteresisFactor(type) : 1.0f; }
extern "C" SDK_EXPORT bool __CDECL Streamer_SetHysteresisFactor(int type, float value)
{ return g_streamer && g_streamer->setHysteresisFactor(type, value); }

// ============================================================================
// Two-tier grid (VS:RP fork).
// ============================================================================

extern "C" SDK_EXPORT float __CDECL Streamer_GetCoarseCellSize()
{ return g_streamer ? g_streamer->getCoarseCellSize() : 0.0f; }
extern "C" SDK_EXPORT bool __CDECL Streamer_SetCoarseCellSize(float size)
{ return g_streamer && g_streamer->setCoarseCellSize(size); }
extern "C" SDK_EXPORT float __CDECL Streamer_GetCoarseCellDistance()
{ return g_streamer ? g_streamer->getCoarseCellDistance() : 0.0f; }
extern "C" SDK_EXPORT bool __CDECL Streamer_SetCoarseCellDistance(float distance)
{ return g_streamer && g_streamer->setCoarseCellDistance(distance); }

extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_PickUpDynamicPickup(FnPlayerInt fn)      { cb_pickup = fn; }
extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_EnterDynamicCP(FnPlayerInt fn)          { cb_enterCp = fn; }
extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_LeaveDynamicCP(FnPlayerInt fn)          { cb_leaveCp = fn; }
extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_EnterDynamicRaceCP(FnPlayerInt fn)      { cb_enterRaceCp = fn; }
extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_LeaveDynamicRaceCP(FnPlayerInt fn)      { cb_leaveRaceCp = fn; }
extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_EnterDynamicArea(FnPlayerInt fn)        { cb_enterArea = fn; }
extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_LeaveDynamicArea(FnPlayerInt fn)        { cb_leaveArea = fn; }
extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_DynamicObjectMoved(FnInt fn)            { cb_objectMoved = fn; }
extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_DynamicObjectStreamIn(FnIntInt fn)      { cb_objStreamIn = fn; }
extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_DynamicObjectStreamOut(FnIntInt fn)     { cb_objStreamOut = fn; }
extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_DynamicPickupStreamIn(FnIntInt fn)      { cb_pkpStreamIn = fn; }
extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_DynamicPickupStreamOut(FnIntInt fn)     { cb_pkpStreamOut = fn; }
extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_DynamicTextLabelStreamIn(FnIntInt fn)   { cb_lblStreamIn = fn; }
extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_DynamicTextLabelStreamOut(FnIntInt fn)  { cb_lblStreamOut = fn; }
extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_DynamicCheckpointStreamIn(FnIntInt fn)  { cb_cpStreamIn = fn; }
extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_DynamicCheckpointStreamOut(FnIntInt fn) { cb_cpStreamOut = fn; }
extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_DynamicMapIconStreamIn(FnIntInt fn)     { cb_iconStreamIn = fn; }
extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_DynamicMapIconStreamOut(FnIntInt fn)    { cb_iconStreamOut = fn; }
extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_PlayerEditDynamicObject(FnEdit fn)      { cb_editObj = fn; }
extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_PlayerSelectDynamicObject(FnSelect fn)  { cb_selectObj = fn; }
extern "C" SDK_EXPORT void __CDECL Streamer_SetCallback_PlayerShootDynamicObject(FnShoot fn)    { cb_shootObj = fn; }
