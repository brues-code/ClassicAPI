// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

// `PlayerModel:SetDisplayInfo(displayID)` and `PlayerModel:SetCreature(creatureID)`
// — 4.3.4's CGCharacterModelBase methods (CharacterModelBase_Scripts.cpp), on
// vanilla's own pane path.
//
// Vanilla's PlayerModel already shows a creature display: SetDisplay
// (FUN_PLAYERMODEL_SET_DISPLAY) stores a creature-cache record in the pane and
// AttachDisplayModel (FUN_PLAYERMODEL_ATTACH_DISPLAY_MODEL) builds the model
// from the record's display ID — skins via ApplyCreatureDisplay, Stand armed,
// the done callback, the pane camera, and a rebuild every time the pane is
// shown again. The pet stable drives it. What it lacks is 4.3.4's character
// step: SetUpModel dresses a display with a CreatureDisplayInfoExtra record
// through the character compositor, where vanilla leaves a humanoid NPC
// white-skinned with every geoset on.
//
// So:
//   - AttachDisplayModel is co-hooked to add that step: after the original
//     builds the model, a character display is dressed through the engine
//     compositor (the DressJob below, a field-for-field mirror of the unit
//     path CGUnit_C::UpdateCharacterCustomization).
//   - SetCreature passes the creature's cache record straight to SetDisplay —
//     the same record 4.3.4 passes (CreatureStats_C) and the stable passes.
//   - SetDisplayInfo has a display ID and no cache record. The pane only ever
//     reads the record's display-ID field (OFF_PLAYERMODEL_DISPLAY_RECORD), so
//     each pane gets a record of its own holding just that field, which lives
//     as long as the pane can point at it.
//
// SetCreature only resolves creatures already in the cache (creaturecache.wdb
// or queried this session); an uncached entry is a no-op. A caller can
// `C_CreatureInfo.RequestLoadCreatureByID` first and call this after
// `CREATURE_DATA_LOAD_RESULT`.
//
// Still not shown: weapons. NPC weapons come from the server-pushed
// UNIT_VIRTUAL_ITEM_SLOT_DISPLAY fields on live units, not from any client
// DBC, so a display ID alone can't produce them.

#include "Game.h"
#include "Offsets.h"
#include "creature/Info.h"
#include "dbc/Lookup.h"
#include "model/DisplayInfo.h"
#include "tick/WorldTick.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace Model::DisplayInfo {
namespace {

// The compositor info struct FUN_CHARCOMP_SET_INFO copies (0x5B dwords).
// Layout read straight out of FUN_005fb200's NPC branch.
struct CharCompInfo {
    uint32_t race;               // +0x00  Extra +0x04
    uint32_t sex;                // +0x04  Extra +0x08
    uint32_t hairColor;          // +0x08  Extra +0x18
    uint32_t skin;               // +0x0C  Extra +0x0C
    uint32_t zero;               // +0x10  engine passes 0
    uint32_t face;               // +0x14  Extra +0x10
    uint32_t facialHair;         // +0x18  Extra +0x1C
    uint32_t hairStyle;          // +0x1C  Extra +0x14
    void *model;                 // +0x20  AddRef'd by caller; builder owns the ref
    uint8_t useBake;             // +0x24  1 = load bakePath instead of compositing
    char bakePath[263];          // +0x25  "Textures\BakedNpcTextures\<bake>"
    uint32_t geosetDefaults[16]; // +0x12C per-group defaults (see kGeosetGroupDefaults)
};
static_assert(sizeof(CharCompInfo) == 0x16C, "must match the engine's 0x5B-dword copy");

// The engine's geoset-group defaults from FUN_005fb200 — group N shows
// N*100+1, except ears (group 7) whose "shown" variant is 702. The hair and
// facial-hair groups get overwritten by the compositor's CharHairGeosets /
// CharacterFacialHairStyles resolution.
constexpr uint32_t kGeosetGroupDefaults[16] = {1,   101,  201,  301,  401,  501,
                                               601, 702,  801,  901,  1001, 1101,
                                               1201, 1301, 1401, 1501};

using CharCompCreate_t = void *(__cdecl *)();
using CharCompDestroy_t = void(__fastcall *)(void *builder);
using CharCompSetInfo_t = char(__thiscall *)(void *builder, const CharCompInfo *info);
using CharCompSetItem_t = void(__thiscall *)(void *builder, int slot, int itemDisplayID);
using CharCompPump_t = char(__thiscall *)(void *builder, int *unused);
using ModelAddRef_t = void(__fastcall *)(void *instance);
using ModelLoaded_t = int(__thiscall *)(void *instance, int tryLoad, int recurseChildren);

// A pending / finished dress for one Model frame. The model loads async, and
// the engine only dresses a LOADED model (its poll gate FUN_00607da0 checks
// FUN_MODEL_INSTANCE_LOADED first), so each job waits on WorldTick for the
// load, configures the compositor once, then pumps it to completion. The
// builder stays alive after that — units keep theirs for the model's whole
// lifetime, and it holds the ref on the model instance — until the frame
// loads another display or the reload teardown.
struct DressJob {
    void *frame = nullptr;         // CSimpleModelFFX that requested the dress
    void *instance = nullptr;      // model instance the job is bound to
    const uint8_t *extraRec = nullptr; // CreatureDisplayInfoExtra (DBC memory, stable)
    void *builder = nullptr;       // engine compositor (owned)
    bool configured = false;
    bool done = false;
    int ticks = 0;
};

// Model frames are few and permanent; SetDisplayInfo users fewer still.
DressJob g_jobs[8];
// Give up on a model that never finishes loading (bad path, missing file).
constexpr int kMaxDressTicks = 600;

void CancelJob(DressJob &job) {
    if (job.builder != nullptr)
        reinterpret_cast<CharCompDestroy_t>(Offsets::FUN_CHARCOMP_DESTROY)(job.builder);
    job = DressJob{};
}

DressJob *FindJob(void *frame) {
    for (auto &job : g_jobs)
        if (job.frame == frame)
            return &job;
    return nullptr;
}

// Claim a slot for `frame`: its own existing slot, a free one, or — all busy —
// a finished one (the dressed model keeps its geosets and texture refs; only
// the kept-alive builder is dropped early).
DressJob *ClaimJob(void *frame) {
    DressJob *slot = FindJob(frame);
    if (slot == nullptr)
        for (auto &job : g_jobs)
            if (job.frame == nullptr) {
                slot = &job;
                break;
            }
    if (slot == nullptr)
        for (auto &job : g_jobs)
            if (job.done) {
                slot = &job;
                break;
            }
    if (slot == nullptr)
        slot = &g_jobs[0];
    CancelJob(*slot);
    return slot;
}

// Queue a dress for a character display just loaded into `frame`. Mirrors the
// engine's own early-out: an Extra record with no bake name gets no
// customization at all (FUN_005fb200 returns before creating the builder).
void ScheduleDress(void *frame, const uint8_t *extraRec) {
    const char *bake = Game::Read<const char *>(
        extraRec, Offsets::OFF_CREATUREDISPLAYINFOEXTRA_BAKE_NAME);
    if (bake == nullptr || bake[0] == '\0')
        return;
    DressJob *job = ClaimJob(frame);
    job->frame = frame;
    job->instance = Game::Read<void *>(frame,
                                       Offsets::OFF_SIMPLEMODELFFX_MODEL_INSTANCE);
    job->extraRec = extraRec;
}

// One WorldTick step for one job: wait for the load, then configure + feed
// equipment (the engine's order: SetInfo, then the 10 Extra equipment slots),
// then pump until done. Field-for-field mirror of FUN_005fb200's NPC branch.
void StepJob(DressJob &job) {
    void *instance = Game::Read<void *>(job.frame,
                                        Offsets::OFF_SIMPLEMODELFFX_MODEL_INSTANCE);
    if (job.instance == nullptr)
        job.instance = instance;
    if (instance == nullptr || instance != job.instance || ++job.ticks > kMaxDressTicks) {
        CancelJob(job); // model swapped out from under us / never loaded
        return;
    }

    if (!job.configured) {
        if (!reinterpret_cast<ModelLoaded_t>(Offsets::FUN_MODEL_INSTANCE_LOADED)(instance, 0, 0))
            return;

        void *builder = reinterpret_cast<CharCompCreate_t>(Offsets::FUN_CHARCOMP_CREATE)();
        if (builder == nullptr) {
            CancelJob(job);
            return;
        }
        job.builder = builder;

        const uint8_t *extra = job.extraRec;
        auto field = [extra](int off) {
            return Game::Read<uint32_t>(extra, off);
        };
        CharCompInfo info = {};
        info.race = field(Offsets::OFF_CREATUREDISPLAYINFOEXTRA_RACE);
        info.sex = field(Offsets::OFF_CREATUREDISPLAYINFOEXTRA_SEX);
        info.hairColor = field(Offsets::OFF_CREATUREDISPLAYINFOEXTRA_HAIR_COLOR);
        info.skin = field(Offsets::OFF_CREATUREDISPLAYINFOEXTRA_SKIN);
        info.face = field(Offsets::OFF_CREATUREDISPLAYINFOEXTRA_FACE);
        info.facialHair = field(Offsets::OFF_CREATUREDISPLAYINFOEXTRA_FACIAL_HAIR);
        info.hairStyle = field(Offsets::OFF_CREATUREDISPLAYINFOEXTRA_HAIR_STYLE);
        info.model = instance;
        info.useBake = 1;
        const char *bake = Game::Read<const char *>(
            extra, Offsets::OFF_CREATUREDISPLAYINFOEXTRA_BAKE_NAME);
        std::snprintf(info.bakePath, sizeof(info.bakePath), "%s%s",
                      "Textures\\BakedNpcTextures\\", bake);
        std::memcpy(info.geosetDefaults, kGeosetGroupDefaults, sizeof(kGeosetGroupDefaults));

        // The builder owns this ref (releases it on destroy / re-configure).
        reinterpret_cast<ModelAddRef_t>(Offsets::FUN_MODEL_INSTANCE_ADDREF)(instance);
        if (!reinterpret_cast<CharCompSetInfo_t>(Offsets::FUN_CHARCOMP_SET_INFO)(builder, &info)) {
            CancelJob(job); // destroy releases the model ref already copied in
            return;
        }
        auto setItem = reinterpret_cast<CharCompSetItem_t>(Offsets::FUN_CHARCOMP_SET_ITEM);
        for (int slot = 0; slot < 10; ++slot) {
            const auto displayID = static_cast<int>(
                field(Offsets::OFF_CREATUREDISPLAYINFOEXTRA_EQUIP + slot * 4));
            if (displayID != 0)
                setItem(builder, slot, displayID);
        }

        // The helm/shoulder feeds above attach child model instances. The frame
        // only lights its OWN instance (pre-render callback registered by
        // SetModelInstance), so the children would render unlit — pure black.
        // Give each child the frame's own light/fog callback; it reads the
        // frame's live light struct, so later SetLight calls reach them too.
        using SetRenderCb_t = void(__thiscall *)(void *instance, const void *cb, void *userData);
        auto setRenderCb =
            reinterpret_cast<SetRenderCb_t>(Offsets::FUN_MODEL_INSTANCE_SET_RENDER_CB);
        const void *lightFogCb =
            reinterpret_cast<const void *>(Offsets::FUN_SIMPLEMODELFFX_LIGHT_FOG_CB);
        for (auto *child = Game::Read<uint8_t *>(
                 instance, Offsets::OFF_MODEL_INSTANCE_CHILD_HEAD);
             child != nullptr; child = Game::Read<uint8_t *>(
                                   child, Offsets::OFF_MODEL_INSTANCE_CHILD_NEXT))
            setRenderCb(child, lightFogCb, job.frame);

        job.configured = true;
    }

    if (reinterpret_cast<CharCompPump_t>(Offsets::FUN_CHARCOMP_PUMP)(job.builder, nullptr))
        job.done = true; // builder kept — it owns the model ref for the dressed model
}

void OnWorldTick() {
    for (auto &job : g_jobs)
        if (job.frame != nullptr && !job.done)
            StepJob(job);
}

const Tick::WorldTick::AutoSubscribe g_tick{&OnWorldTick};

// ---- AttachDisplayModel co-hook ---------------------------------------------

using AttachDisplayModel_t = void(__thiscall *)(void *pane, const uint8_t *record);
AttachDisplayModel_t g_origAttach = nullptr;

// 4.3.4's SetUpModel order: build the model, then dress it when the display
// has an Extra record.
void __fastcall AttachDisplayModel_h(void *pane, void * /*edx*/, const uint8_t *record) {
    // Any pending or kept dress for this pane is about to point at the wrong
    // model — drop it before the original swaps the instance.
    if (DressJob *job = FindJob(pane))
        CancelJob(*job);

    g_origAttach(pane, record);

    if (record == nullptr)
        return;
    const uint8_t *displayRec = DBC::Record(
        Offsets::VAR_CREATUREDISPLAYINFO_RECORDS, Offsets::VAR_CREATUREDISPLAYINFO_COUNT,
        Game::Read<uint32_t>(record, Offsets::OFF_CREATURE_DISPLAYID));
    if (displayRec == nullptr)
        return;
    const uint32_t extID =
        Game::Read<uint32_t>(displayRec, Offsets::OFF_CREATUREDISPLAYINFO_EXTENDED_ID);
    if (extID == 0)
        return;
    const uint8_t *extraRec = DBC::Record(Offsets::VAR_CREATUREDISPLAYINFOEXTRA_RECORDS,
                                          Offsets::VAR_CREATUREDISPLAYINFOEXTRA_COUNT, extID);
    if (extraRec != nullptr)
        ScheduleDress(pane, extraRec);
}

const Game::HookAutoRegister _attachHook{
    Offsets::FUN_PLAYERMODEL_ATTACH_DISPLAY_MODEL,
    reinterpret_cast<void *>(&AttachDisplayModel_h),
    reinterpret_cast<void **>(&g_origAttach)};

// ---- Lua methods --------------------------------------------------------------

using SetDisplay_t = void(__thiscall *)(void *pane, const uint8_t *record);

void SetDisplay(void *pane, const uint8_t *record) {
    reinterpret_cast<SetDisplay_t>(Offsets::FUN_PLAYERMODEL_SET_DISPLAY)(pane, record);
}

// The record a pane shows for SetDisplayInfo: only the display-ID field is
// ever read. One per pane, never freed — the pane keeps the pointer and
// rebuilds from it on every show, and panes live as long as the UI.
struct DisplayRecord {
    uint8_t bytes[Offsets::OFF_CREATURE_DISPLAYID + sizeof(uint32_t)];
};
std::unordered_map<void *, DisplayRecord> g_displayRecords;

int __fastcall Script_SetDisplayInfo(void *L) {
    void *pane = Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_PLAYERMODEL_LUA_TYPE_ID);
    if (pane == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: SetDisplayInfo(displayID)");
        return 0;
    }
    const auto displayID = static_cast<uint32_t>(Game::Lua::ToNumber(L, 2));
    if (displayID == 0)
        return 0;
    DisplayRecord &rec = g_displayRecords[pane];
    std::memset(rec.bytes, 0, sizeof(rec.bytes));
    std::memcpy(rec.bytes + Offsets::OFF_CREATURE_DISPLAYID, &displayID, sizeof(displayID));
    SetDisplay(pane, rec.bytes);
    return 0;
}

int __fastcall Script_SetCreature(void *L) {
    void *pane = Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_PLAYERMODEL_LUA_TYPE_ID);
    if (pane == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: SetCreature(creatureID)");
        return 0;
    }
    const uint8_t *record =
        Creature::Info::CacheRecord(static_cast<uint32_t>(Game::Lua::ToNumber(L, 2)));
    if (record != nullptr)
        SetDisplay(pane, record);
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetDisplayInfo", &Script_SetDisplayInfo},
    {"SetCreature", &Script_SetCreature},
};

const Game::Doc::Field kDisplayArgs[] = {
    Game::Doc::Req("displayID", "number", "A CreatureDisplayInfo ID."),
};
const Game::Doc::Function kSetDisplayInfo{
    "Shows a creature display on the model, dressed like the creature.", kDisplayArgs, {}};
const Game::Doc::Field kCreatureArgs[] = {
    Game::Doc::Req("creatureID", "number", "A creature ID that is in the creature cache."),
};
const Game::Doc::Function kSetCreature{
    "Shows a creature on the model by its creature ID.", kCreatureArgs, {}};
const Game::Doc::Method g_methodDocs[] = {
    {"SetDisplayInfo", &kSetDisplayInfo},
    {"SetCreature", &kSetCreature},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_PLAYERMODEL_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void PrepareForReload() {
    for (auto &job : g_jobs)
        CancelJob(job);
}

static const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

} // namespace Model::DisplayInfo
