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

// `PlayerModel:SetDisplayInfo(displayID)` and
// `PlayerModel:SetCreature(creatureID [, displayID])`
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
//     Retail's optional displayID shows that display in its place, through
//     the SetDisplayInfo record below.
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
#include "model/ItemDisplay.h"
#include "model/M2.h"
#include "model/Teardown.h"
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

// A pending / finished dress for one Model frame. The model loads async, and
// the engine only dresses a LOADED model (its poll gate FUN_00607da0 checks
// FUN_MODEL_INSTANCE_LOADED first), so each job waits on WorldTick for the
// load, configures the compositor once, then pumps it to completion. The
// builder stays alive after that — units keep theirs for the model's whole
// lifetime, and it holds the ref on the model instance — until the frame
// loads another display or the reload teardown.
struct DressJob {
    void *instance = nullptr;          // model instance the job is bound to
    const uint8_t *extraRec = nullptr; // CreatureDisplayInfoExtra (DBC memory, stable)
    void *builder = nullptr;           // engine compositor (owned)
    bool configured = false;
    bool done = false;
    int ticks = 0;
};
std::unordered_map<void *, DressJob> g_jobs; // by frame
// Give up on a model that never finishes loading (bad path, missing file).
constexpr int kMaxDressTicks = 600;

void CancelJob(void *frame) {
    const auto it = g_jobs.find(frame);
    if (it == g_jobs.end())
        return;
    DestroyDresser(it->second.builder);
    g_jobs.erase(it);
}

// The frame's scene is about to free the model its builder holds.
const Model::Teardown::AutoSubscribe _teardown{&CancelJob};

// One WorldTick step for one job: wait for the load, then configure + feed
// equipment, then pump until done. False once the job is dead.
bool StepJob(void *frame, DressJob &job) {
    void *instance = Pane::Model(frame);
    if (job.instance == nullptr)
        job.instance = instance;
    if (instance == nullptr || instance != job.instance || ++job.ticks > kMaxDressTicks)
        return false; // model swapped out from under us / never loaded

    if (!job.configured) {
        if (!M2::IsLoaded(instance, 0, 0))
            return true;
        job.builder = CreateDresser(instance, job.extraRec);
        if (job.builder == nullptr)
            return false;
        // The helm/shoulder feeds attach child model instances.
        LightChildren(frame, instance);
        job.configured = true;
    }

    if (PumpDresser(job.builder))
        job.done = true; // builder kept — it owns the model ref for the dressed model
    return true;
}

void OnWorldTick() {
    for (auto it = g_jobs.begin(); it != g_jobs.end();) {
        if (it->second.done || StepJob(it->first, it->second)) {
            ++it;
            continue;
        }
        DestroyDresser(it->second.builder);
        it = g_jobs.erase(it);
    }
}

const Tick::WorldTick::AutoSubscribe g_tick{&OnWorldTick};

// ---- AttachDisplayModel co-hook ---------------------------------------------

using AttachDisplayModel_t = void(__thiscall *)(void *pane, const uint8_t *record);
AttachDisplayModel_t g_origAttach = nullptr;

// 4.3.4's SetUpModel order: build the model, then dress it when the display
// has an Extra record.
// An item record (SetItem) builds the item's model instead.
void __fastcall AttachDisplayModel_h(void *pane, void * /*edx*/, const uint8_t *record) {
    // Any pending or kept dress for this pane is about to point at the wrong
    // model — drop it before the original swaps the instance.
    CancelJob(pane);

    if (Model::ItemDisplay::Attach(pane, record))
        return;
    g_origAttach(pane, record);

    if (record == nullptr)
        return;
    if (const uint8_t *extraRec =
            DressRecord(Game::Read<uint32_t>(record, Offsets::OFF_CREATURE_DISPLAYID)))
        g_jobs[pane] = DressJob{Pane::Model(pane), extraRec};
}

const Game::HookAutoRegister _attachHook{
    Offsets::FUN_PLAYERMODEL_ATTACH_DISPLAY_MODEL,
    reinterpret_cast<void *>(&AttachDisplayModel_h),
    reinterpret_cast<void **>(&g_origAttach)};

// ---- Lua methods --------------------------------------------------------------

// The record each pane shows for SetDisplayInfo.
std::unordered_map<void *, Pane::DisplayRecord> g_displayRecords;

void ShowDisplayID(void *pane, uint32_t displayID) {
    if (displayID == 0)
        return;
    Pane::DisplayRecord &rec = g_displayRecords[pane];
    rec = {};
    Game::Ref<uint32_t>(rec.bytes, Offsets::OFF_CREATURE_DISPLAYID) = displayID;
    Pane::SetDisplay(pane, rec.bytes);
}

int __fastcall Script_SetDisplayInfo(void *L) {
    void *pane = Game::Lua::ResolvePlayerModel(L);
    if (pane == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: SetDisplayInfo(displayID)");
        return 0;
    }
    ShowDisplayID(pane, static_cast<uint32_t>(Game::Lua::ToNumber(L, 2)));
    return 0;
}

int __fastcall Script_SetCreature(void *L) {
    void *pane = Game::Lua::ResolvePlayerModel(L);
    if (pane == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: SetCreature(creatureID [, displayID])");
        return 0;
    }
    const uint8_t *record =
        Creature::Info::CacheRecord(static_cast<uint32_t>(Game::Lua::ToNumber(L, 2)));
    if (record == nullptr)
        return 0;
    if (Game::Lua::IsNumber(L, 3))
        ShowDisplayID(pane, static_cast<uint32_t>(Game::Lua::ToNumber(L, 3)));
    else
        Pane::SetDisplay(pane, record);
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
    Game::Doc::Opt("displayID", "number", nullptr,
                   "The CreatureDisplayInfo ID to show in place of the creature's own."),
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
        g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// Frames are rebuilt on /reload and a new frame can reuse an old address. The
// dress jobs are gone by now: each went with its frame (Model::Teardown).
void PrepareForReload() {
    g_displayRecords.clear();
}

const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

} // namespace

// The engine's own early-out: an Extra record with no bake name gets no
// customization at all (FUN_005fb200 returns before creating the builder).
const uint8_t *DressRecord(uint32_t displayID) {
    const uint8_t *displayRec = DBC::Record(Offsets::VAR_CREATUREDISPLAYINFO_RECORDS,
                                            Offsets::VAR_CREATUREDISPLAYINFO_COUNT, displayID);
    if (displayRec == nullptr)
        return nullptr;
    const uint32_t extID =
        Game::Read<uint32_t>(displayRec, Offsets::OFF_CREATUREDISPLAYINFO_EXTENDED_ID);
    if (extID == 0)
        return nullptr;
    const uint8_t *extraRec = DBC::Record(Offsets::VAR_CREATUREDISPLAYINFOEXTRA_RECORDS,
                                          Offsets::VAR_CREATUREDISPLAYINFOEXTRA_COUNT, extID);
    if (extraRec == nullptr)
        return nullptr;
    const char *bake =
        Game::Read<const char *>(extraRec, Offsets::OFF_CREATUREDISPLAYINFOEXTRA_BAKE_NAME);
    return bake != nullptr && bake[0] != '\0' ? extraRec : nullptr;
}

// Configure + feed equipment, the engine's order: SetInfo, then the 10 Extra
// equipment slots. Field-for-field mirror of FUN_005fb200's NPC branch.
void *CreateDresser(void *instance, const uint8_t *extraRec) {
    void *builder = reinterpret_cast<CharCompCreate_t>(Offsets::FUN_CHARCOMP_CREATE)();
    if (builder == nullptr)
        return nullptr;

    auto field = [extraRec](int off) {
        return Game::Read<uint32_t>(extraRec, off);
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
    const char *bake =
        Game::Read<const char *>(extraRec, Offsets::OFF_CREATUREDISPLAYINFOEXTRA_BAKE_NAME);
    std::snprintf(info.bakePath, sizeof(info.bakePath), "%s%s", "Textures\\BakedNpcTextures\\",
                  bake);
    std::memcpy(info.geosetDefaults, kGeosetGroupDefaults, sizeof(kGeosetGroupDefaults));

    // The builder owns this ref (releases it on destroy / re-configure).
    M2::AddRef(instance);
    if (!reinterpret_cast<CharCompSetInfo_t>(Offsets::FUN_CHARCOMP_SET_INFO)(builder, &info)) {
        DestroyDresser(builder); // releases the model ref already copied in
        return nullptr;
    }
    auto setItem = reinterpret_cast<CharCompSetItem_t>(Offsets::FUN_CHARCOMP_SET_ITEM);
    for (int slot = 0; slot < 10; ++slot) {
        const auto displayID =
            static_cast<int>(field(Offsets::OFF_CREATUREDISPLAYINFOEXTRA_EQUIP + slot * 4));
        if (displayID != 0)
            setItem(builder, slot, displayID);
    }
    return builder;
}

// The frame only lights its OWN instance (the pre-render callback
// SetModelInstance registers), so attached children would render unlit, pure
// black. Each child gets the frame's own light/fog callback; it reads the
// frame's live light struct, so later SetLight calls reach them too.
void LightChildren(void *frame, void *instance) {
    const void *lightFogCb =
        reinterpret_cast<const void *>(Offsets::FUN_SIMPLEMODELFFX_LIGHT_FOG_CB);
    for (auto *child = Game::Read<uint8_t *>(instance, Offsets::OFF_MODEL_INSTANCE_CHILD_HEAD);
         child != nullptr;
         child = Game::Read<uint8_t *>(child, Offsets::OFF_MODEL_INSTANCE_CHILD_NEXT))
        M2::SetRenderCallback(child, lightFogCb, frame);
}

bool PumpDresser(void *dresser) {
    return reinterpret_cast<CharCompPump_t>(Offsets::FUN_CHARCOMP_PUMP)(dresser, nullptr) != 0;
}

void DestroyDresser(void *dresser) {
    if (dresser != nullptr)
        reinterpret_cast<CharCompDestroy_t>(Offsets::FUN_CHARCOMP_DESTROY)(dresser);
}

} // namespace Model::DisplayInfo
