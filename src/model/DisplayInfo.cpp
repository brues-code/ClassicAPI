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

// `Model:SetDisplayInfo(creatureDisplayID)` — backport of the modern Model
// method that points a Model frame at a creature by its display ID, instead of
// requiring a `.mdx` file path (`Model:SetModel`). Vanilla already loads models
// by path; this just prepends the DBC resolution modern clients do internally:
//
//   creatureDisplayID → CreatureDisplayInfo[id] +0x04 ModelID
//                     → CreatureModelData[ModelID] +0x08 ModelPath
//                     → Model:SetModel's worker (vtable +0x94)
//                     → apply the display's TextureVariation skins
//                     → character displays: dress via the engine compositor
//                       (baked skin, hair, geosets, equipment — see the
//                       "Character-display dressing" section below)
//
// The load path is the engine's own (`Script_SetModel` FUN_0076d950 calls the
// same vtable +0x94 worker); we add the display-ID front end and the same
// dressing a spawned unit gets. It is the foundational "display ID → loaded
// model" step — a rendered portrait-by-display-ID
// (SetPortraitTextureFromCreatureDisplayID) would build render-to-texture on
// top of this, but a live Model frame needs neither.
//
// SetCreature(creatureID) is the entry-ID cousin: it resolves through the client
// creature cache to a display ID, then runs the same load — see its note below.

#include "Game.h"
#include "Offsets.h"
#include "creature/Info.h"
#include "dbc/Lookup.h"
#include "model/DisplayInfo.h"
#include "tick/WorldTick.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace Model::DisplayInfo {
namespace {

// The Model model-load worker at vtable +0x94 — the one Script_SetModel
// invokes after resolving self. Single-use here, so kept local.
constexpr int kVmtLoadModel = 0x94;

using LoadModel_t = void(__thiscall *)(void *self, const char *path);
using SetReplaceableTexture_t = void(__thiscall *)(void *model, int type, const char *path);

template <typename Fn>
Fn Vmethod(void *obj, int byteOffset) {
    auto *vtbl = *reinterpret_cast<uint8_t **>(obj);
    return *reinterpret_cast<Fn *>(vtbl + byteOffset);
}

// Resolve arg1 to a Model object, or nullptr if it is not a Model. The
// type-check matters especially here because we call a Model vtable slot
// directly — a non-Model region would dispatch a wrong method. Non-raising,
// which is the contract the callers below already had.
void *ResolveModel(void *L) {
    return Game::Lua::ResolveModel(L, 1, /*raiseError=*/false);
}

// Apply a creature display's TextureVariation skins to the just-loaded model.
// The variation columns are bare filenames that live in the model's own
// directory; they fill the model's MONSTER_1/2/3 replaceable-texture slots
// (M2/MDX texture types 11/12/13). FUN_0076cfe0 no-ops when no model is loaded,
// so this is safe to call unconditionally after the load.
void ApplySkins(void *model, const char *modelPath, const uint8_t *displayRec) {
    std::string dir(modelPath);
    const auto slash = dir.find_last_of("\\/");
    dir = (slash == std::string::npos) ? std::string() : dir.substr(0, slash + 1);

    auto setTexture = reinterpret_cast<SetReplaceableTexture_t>(
        Offsets::FUN_MODEL_SET_REPLACEABLE_TEXTURE);
    constexpr int kMonsterSlot[3] = {11, 12, 13};
    for (int i = 0; i < 3; ++i) {
        const char *variation = Game::Read<const char *>(
            displayRec, Offsets::OFF_CREATUREDISPLAYINFO_TEXTURE_VARIATION + i * 4);
        if (variation == nullptr || variation[0] == '\0')
            continue;
        const std::string skin = dir + variation;
        setTexture(model, kMonsterSlot[i], skin.c_str());
    }
}

// --- Character-display dressing via the engine compositor -------------------
//
// Character-based displays (CreatureDisplayInfo.extendedDisplayInfoID != 0) use
// a shared Character\Race\Sex base model that the engine "dresses" per display:
// baked body texture, hair texture, hair / facial-hair geosets (beards,
// earrings, teeth), and equipment geosets (sleeves, robe→trousers, boots,
// tabard, cape). Units run this through a compositor object
// (CGUnit_C::UpdateCharacterCustomization, FUN_005fb200) once their model
// loads; SetPortraitTexture waits on the same compositor, which is why
// portraits always look right. We drive the identical compositor for the
// Model frame, mirroring the unit flow field for field — see the
// FUN_CHARCOMP_* notes in Offsets.h.
//
// Still not shown: weapons. NPC weapons come from the server-pushed
// UNIT_VIRTUAL_ITEM_SLOT_DISPLAY fields on live units, not from any client
// DBC, so a display ID alone can't produce them.

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

// Resolve a creature display ID to its model file and skins, and load it into
// the Model frame. Shared by SetDisplayInfo (display ID direct) and SetCreature
// (creature entry ID -> cache -> display ID). No-op for an unknown display ID.
void LoadDisplay(void *model, int displayID) {
    if (displayID <= 0)
        return;
    const uint8_t *displayRec = DBC::Record(Offsets::VAR_CREATUREDISPLAYINFO_RECORDS,
                                            Offsets::VAR_CREATUREDISPLAYINFO_COUNT,
                                            static_cast<uint32_t>(displayID));
    if (displayRec == nullptr)
        return;
    const uint32_t modelID = Game::Read<uint32_t>(
        displayRec, Offsets::OFF_CREATUREDISPLAYINFO_MODEL_ID);

    const char *modelPath = DBC::StringField(Offsets::VAR_CREATUREMODELDATA_RECORDS,
                                             Offsets::VAR_CREATUREMODELDATA_COUNT,
                                             modelID,
                                             Offsets::OFF_CREATUREMODELDATA_MODEL_PATH);
    if (modelPath == nullptr)
        return;

    // Any pending or kept dress for this frame is about to point at the
    // wrong model — drop it before the load swaps the instance.
    if (DressJob *job = FindJob(model))
        CancelJob(*job);

    Vmethod<LoadModel_t>(model, kVmtLoadModel)(model, modelPath);
    ApplySkins(model, modelPath, displayRec);

    // Character-based display (Character\Race\Sex base): dress it through the
    // engine compositor, else body and hair render white with every geoset on.
    const uint32_t extID = Game::Read<uint32_t>(
        displayRec, Offsets::OFF_CREATUREDISPLAYINFO_EXTENDED_ID);
    if (extID != 0) {
        const uint8_t *extraRec = DBC::Record(Offsets::VAR_CREATUREDISPLAYINFOEXTRA_RECORDS,
                                              Offsets::VAR_CREATUREDISPLAYINFOEXTRA_COUNT, extID);
        if (extraRec != nullptr)
            ScheduleDress(model, extraRec);
    }
}

int __fastcall Script_SetDisplayInfo(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: Model:SetDisplayInfo(creatureDisplayID)");
        return 0;
    }
    void *model = ResolveModel(L);
    if (model == nullptr) {
        Game::Lua::Error(L, "Model:SetDisplayInfo(): self is not a Model");
        return 0;
    }
    LoadDisplay(model, static_cast<int>(Game::Lua::ToNumber(L, 2)));
    return 0;
}

// `Model:SetCreature(creatureID)` — the creature-entry cousin of SetDisplayInfo.
// Vanilla has no client-side creature-entry -> display table in a DBC; that
// mapping lives in the creature cache (creaturecache.wdb / SMSG_CREATURE_QUERY_
// RESPONSE), keyed by entry ID and carrying the display ID. So this resolves the
// entry through the cache, then loads exactly as SetDisplayInfo does.
//
// Limitation: only creatures already cached (queried this session or loaded from
// the WDB at login) resolve; an uncached entry is a no-op. A caller that needs an
// uncached creature can `C_CreatureInfo.RequestLoadCreatureByID` first, then call
// this after `CREATURE_DATA_LOAD_RESULT`.
int __fastcall Script_SetCreature(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: Model:SetCreature(creatureID)");
        return 0;
    }
    void *model = ResolveModel(L);
    if (model == nullptr) {
        Game::Lua::Error(L, "Model:SetCreature(): self is not a Model");
        return 0;
    }
    const int creatureID = static_cast<int>(Game::Lua::ToNumber(L, 2));
    if (creatureID <= 0)
        return 0;
    const uint32_t displayID = Creature::Info::DisplayID(static_cast<uint32_t>(creatureID));
    if (displayID == 0)
        return 0; // not cached -> no-op (see limitation above)
    LoadDisplay(model, static_cast<int>(displayID));
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetDisplayInfo", &Script_SetDisplayInfo},
    {"SetCreature", &Script_SetCreature},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_MODEL_METHOD_REGISTRY),
        g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void PrepareForReload() {
    for (auto &job : g_jobs)
        CancelJob(job);
}

static const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

} // namespace Model::DisplayInfo
