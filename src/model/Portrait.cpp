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

// `SetPortraitTextureFromCreatureDisplayID(texture, displayID)` — a creature
// portrait from a display ID alone, with no unit.
//
// Vanilla bakes unit portraits in BakePortrait (0x00524F60, benilla
// ui/portraits): it copies the unit's live, dressed model into a fresh scene,
// draws it with the model's portrait camera into the 64x64 top-left corner of
// the back buffer, reads that back, cuts the round alpha mask in, and keeps
// the texture by display ID. A display ID has no live model, so this builds
// one the way AttachDisplayModel does for a PlayerModel — CreateModelFromPath
// on the CreatureModelData path, then ApplyCreatureDisplay — in a scene of its
// own, dresses it through the engine compositor the way a unit is dressed
// (Model::DisplayInfo), and runs the bake's own sequence on it, call for call.
//
// The texture is bound at once and its pixels arrive later. Each display ID
// owns one callback texture, transparent until baked; the bake writes the
// pixels and re-latches them with GxTexMarkRectDirty, the engine's own path
// for re-baking a cached portrait in place. A region the caller points
// somewhere else before the bake finishes keeps what it was given, so no
// region is tracked.
//
// The display model loads asynchronously. The engine bakes from Lua, before
// anything draws over the corner it borrows; pending displays are stepped at
// the head of the UI render callback, the same point in the frame as an
// OnUpdate script that calls SetPortraitTexture. The bake saves and restores
// exactly what BakePortrait saves (projection, view, viewport, fog).
//
// The bake's ready gate is IsReady(model, 0, 1), which only a DRAWN model
// passes: a model's device textures are made the first time something asks
// for them with wait set (GetTextureDeviceObject), and nothing draws this one.
// So the gate here is the load (IsLoaded) plus the dress, and the bake's own
// IsReady(copy, 1, 1) makes the device textures, as it does for the copy of a
// unit's model.
//
// Portraits are kept until the client leaves the world, the lifetime of the
// engine's display-ID portrait cache (ClearPortraitCaches, from ShutdownGame).
// A model with no portrait camera stays transparent; the engine binds an empty
// texture for one.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "model/Camera.h"
#include "model/DisplayInfo.h"
#include "model/M2.h"
#include "texture/Load.h"
#include "tick/FrameTick.h"

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Model::Portrait {
namespace {

constexpr uint32_t kSize = 64;
constexpr uint32_t kTexels = kSize * kSize;

using CreateScene_t = void *(__cdecl *)();
using ReleaseScene_t = void(__thiscall *)(void *scene);
using CopyModel_t = void *(__thiscall *)(void *scene, void *source, uint32_t flags);
using ApplyDisplay_t = void(__fastcall *)(void *model, const uint8_t *display,
                                          const uint8_t *modelData);
using DetachAll_t = void(__fastcall *)(void *model);
using DrawScene_t = void(__thiscall *)(void *scene, uint32_t pass);
using SetTransforms_t = void(__fastcall *)(void *camera, const float *rect, uint32_t unused);
using GetMatrix_t = void(__fastcall *)(float *m);
using SetMatrix_t = void(__fastcall *)(const float *m);
using GetViewport_t = void(__fastcall *)(float *, float *, float *, float *, float *, float *);
using SetViewport_t = void(__stdcall *)(float, float, float, float, float, float);
using ScreenSize_t = void(__fastcall *)(float *rect);
using DdcToNdc_t = void(__fastcall *)(float *x, float *y, float inX, float inY);
using DeviceGate_t = int(__cdecl *)();
using MasterGet_t = bool(__fastcall *)(uint32_t cap);
using MasterSet_t = void(__fastcall *)(uint32_t cap, uint32_t on);
using SetClearColor_t = void(__stdcall *)(uint32_t argb);
using Clear_t = void(__fastcall *)(uint32_t mask);
using AlphaMask_t = const uint8_t *(__fastcall *)(uint32_t size);
using GetDeviceTexture_t = void *(__fastcall *)(void *texture, uint32_t wait, void *callback);
using MarkDirty_t = void(__fastcall *)(void *gxTex, const int32_t *rect, uint32_t immediate);
using SetImage_t = void(__thiscall *)(void *region, void *texture);

// The engine's GrowableArray<uint32>: capacity, count, data, growth step.
// FUN_GX_READ_PIXELS sets the count to the rectangle's texels, which fit the
// capacity given, so it writes them into `data` in place.
struct TexelArray {
    uint32_t capacity;
    uint32_t count;
    uint32_t *data;
    uint32_t increment;
};
// GetPortraitAlphaMask's result: one alpha byte per texel, from +8.
struct AlphaMask {
    uint32_t reserved[2];
    const uint8_t *alpha;
};

struct Portrait {
    std::array<uint32_t, kTexels> texels{}; // the texture's texel source, kept as long
    void *texture = nullptr;                // HTEXTURE, one reference
    // The display model while it loads and dresses.
    void *scene = nullptr;
    void *model = nullptr;
    const uint8_t *dressRecord = nullptr;
    void *dresser = nullptr;
};
std::unordered_map<uint32_t, std::unique_ptr<Portrait>> g_portraits;
std::vector<Portrait *> g_loading;

void ReleaseModel(Portrait &p) {
    Model::DisplayInfo::DestroyDresser(p.dresser);
    M2::Release(p.model);
    reinterpret_cast<ReleaseScene_t>(Offsets::FUN_M2SCENE_RELEASE)(p.scene);
    p.dresser = p.model = p.scene = nullptr;
    p.dressRecord = nullptr;
}

// BakePortrait from the copy on, against `p.model`. False when it could not
// run this frame (no scene / copy), so the next frame tries again.
bool Bake(Portrait &p) {
    void *source = p.model;
    if (Model::Camera::CameraById(source, 0) == nullptr)
        return true;

    // The 64x64 pixel box at the top-left, in DDC, in the bake's own order
    // and precision: x87 operations, a float store after each (0x005251B7).
    float screen[4] = {};
    reinterpret_cast<ScreenSize_t>(Offsets::FUN_GX_CAPS_SCREEN_SIZE)(screen);
    const float width = static_cast<float>(static_cast<double>(screen[3]) - screen[1]);
    const float height = static_cast<float>(static_cast<double>(screen[2]) - screen[0]);
    const double scaleX = Game::Read<float>(Offsets::VAR_COORD_DDC_WIDTH_SCALE);
    const double scaleY = Game::Read<float>(Offsets::VAR_COORD_DDC_HEIGHT_SCALE);
    float x0 = 0.0f;
    float y1 = static_cast<float>(scaleY);
    float x1 = static_cast<float>(kSize / static_cast<double>(width) * scaleX);
    float y0 = static_cast<float>(scaleY - scaleY / height * kSize);

    void *scene = reinterpret_cast<CreateScene_t>(Offsets::FUN_M2_CREATE_SCENE)();
    if (scene == nullptr)
        return false;
    void *copy = reinterpret_cast<CopyModel_t>(Offsets::FUN_M2SCENE_COPY_MODEL)(scene, source, 0);
    if (copy == nullptr) {
        reinterpret_cast<ReleaseScene_t>(Offsets::FUN_M2SCENE_RELEASE)(scene);
        return false;
    }
    reinterpret_cast<DetachAll_t>(Offsets::FUN_DETACH_ALL_ITEM_MODELS)(copy);
    void *camera = Model::Camera::CameraById(copy, 0);
    M2::PlayAnimation(copy, -1, 0, 0, 0, 1.0f, 0, 1);

    float projection[16];
    float view[16];
    float viewport[6];
    reinterpret_cast<GetMatrix_t>(Offsets::FUN_GX_GET_PROJECTION)(projection);
    reinterpret_cast<GetMatrix_t>(Offsets::FUN_GX_GET_VIEW)(view);
    reinterpret_cast<GetViewport_t>(Offsets::FUN_GX_GET_VIEWPORT)(
        &viewport[0], &viewport[1], &viewport[2], &viewport[3], &viewport[4], &viewport[5]);
    const bool fog = reinterpret_cast<MasterGet_t>(Offsets::FUN_GX_MASTER_ENABLE_GET)(
        Offsets::GX_MASTER_FOG);
    auto setMaster = reinterpret_cast<MasterSet_t>(Offsets::FUN_GX_MASTER_ENABLE_SET);
    setMaster(Offsets::GX_MASTER_FOG, 0);

    const float aspect = static_cast<float>((static_cast<double>(x1) - x0) /
                                            (static_cast<double>(y1) - y0));
    auto toNdc = reinterpret_cast<DdcToNdc_t>(Offsets::FUN_COORD_DDC_TO_NDC);
    toNdc(&x0, &y0, x0, y0);
    toNdc(&x1, &y1, x1, y1);

    // First collection: evaluate the portrait camera.
    M2::IsLoaded(copy, 1, 1);
    M2::SetAnimated(copy, true);
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    M2::AnimateScene(scene, origin);
    const Model::Camera::Vec3 eye = Model::Camera::GetVector(camera, Offsets::CAMERA_PROP_EYE);
    Model::Camera::GetVector(camera, Offsets::CAMERA_PROP_TARGET); // the bake reads it too
    const float rect[4] = {0.0f, 0.0f, 1.0f, aspect};
    reinterpret_cast<SetTransforms_t>(Offsets::FUN_CAMERA_SET_DEVICE_TRANSFORMS)(camera, rect, 0);
    reinterpret_cast<SetViewport_t>(Offsets::FUN_GX_SET_VIEWPORT)(x0, x1, y0, y1, 0.0f, 1.0f);
    reinterpret_cast<SetClearColor_t>(Offsets::FUN_GX_SET_CLEAR_COLOR)(0);
    auto clear = reinterpret_cast<Clear_t>(Offsets::FUN_GX_CLEAR);
    clear(3);

    // Second collection from the portrait eye, then both passes.
    M2::IsReady(copy, 1, 1);
    M2::SetAnimated(copy, true);
    M2::SetVisible(copy, true);
    M2::SetRenderCallback(copy, reinterpret_cast<const void *>(Offsets::FUN_PORTRAIT_LIGHTING),
                          nullptr);
    M2::AnimateScene(scene, &eye.x);
    auto draw = reinterpret_cast<DrawScene_t>(Offsets::FUN_M2SCENE_DRAW);
    draw(scene, 0);
    draw(scene, 1);

    const int32_t box[4] = {0, 0, static_cast<int32_t>(kSize), static_cast<int32_t>(kSize)};
    TexelArray out{kTexels, kTexels, p.texels.data(), kTexels};
    reinterpret_cast<void(__fastcall *)(const int32_t *, TexelArray *)>(
        Offsets::FUN_GX_READ_PIXELS)(box, &out);
    clear(3);
    const auto *mask = reinterpret_cast<const AlphaMask *>(
        reinterpret_cast<AlphaMask_t>(Offsets::FUN_PORTRAIT_ALPHA_MASK)(kSize));
    if (out.data == p.texels.data()) {
        for (uint32_t i = 0; i < kTexels; ++i)
            p.texels[i] = (p.texels[i] & 0x00FFFFFFu) | static_cast<uint32_t>(mask->alpha[i]) << 24;
        void *gxTex = reinterpret_cast<GetDeviceTexture_t>(Offsets::FUN_TEXTURE_GET_RENDERABLE)(
            p.texture, 1, nullptr);
        reinterpret_cast<MarkDirty_t>(Offsets::FUN_GX_TEX_MARK_RECT_DIRTY)(gxTex, box, 1);
    }

    reinterpret_cast<SetMatrix_t>(Offsets::FUN_GX_SET_PROJECTION)(projection);
    reinterpret_cast<SetMatrix_t>(Offsets::FUN_GX_SET_VIEW)(view);
    reinterpret_cast<SetViewport_t>(Offsets::FUN_GX_SET_VIEWPORT)(
        viewport[0], viewport[1], viewport[2], viewport[3], viewport[4], viewport[5]);
    setMaster(Offsets::GX_MASTER_FOG, fog ? 1 : 0);
    M2::Release(copy);
    reinterpret_cast<ReleaseScene_t>(Offsets::FUN_M2SCENE_RELEASE)(scene);
    return true;
}

// One frame for one pending display; true once it is baked (or cannot be).
bool Step(Portrait &p) {
    if (!M2::IsLoaded(p.model, 0, 1))
        return false;
    if (p.dressRecord != nullptr) {
        // A dress the engine rejects bakes undressed, as a unit with no
        // compositor does.
        if (p.dresser == nullptr)
            p.dresser = Model::DisplayInfo::CreateDresser(p.model, p.dressRecord);
        if (p.dresser == nullptr)
            p.dressRecord = nullptr;
        else if (!Model::DisplayInfo::PumpDresser(p.dresser))
            return false;
    }
    if (!reinterpret_cast<DeviceGate_t>(Offsets::FUN_GX_WINDOW_ACTIVE)() ||
        !reinterpret_cast<DeviceGate_t>(Offsets::FUN_GX_DEVICE_ALIVE)())
        return false;
    return Bake(p);
}

void Pump() {
    for (size_t i = 0; i < g_loading.size();) {
        Portrait &p = *g_loading[i];
        if (!Step(p)) {
            ++i;
            continue;
        }
        ReleaseModel(p);
        g_loading[i] = g_loading.back();
        g_loading.pop_back();
    }
}

const Tick::FrameTick::AutoSubscribeBefore _tick{&Pump};

// The display's portrait, started on first use. Null for a display ID with no
// CreatureDisplayInfo / CreatureModelData row (AttachDisplayModel's gate).
Portrait *Acquire(uint32_t displayID) {
    auto found = g_portraits.find(displayID);
    if (found != g_portraits.end())
        return found->second.get();
    const uint8_t *display = DBC::Record(Offsets::VAR_CREATUREDISPLAYINFO_RECORDS,
                                         Offsets::VAR_CREATUREDISPLAYINFO_COUNT, displayID);
    if (display == nullptr)
        return nullptr;
    const uint8_t *modelData = DBC::Record(
        Offsets::VAR_CREATUREMODELDATA_RECORDS, Offsets::VAR_CREATUREMODELDATA_COUNT,
        Game::Read<uint32_t>(display, Offsets::OFF_CREATUREDISPLAYINFO_MODEL_ID));
    if (modelData == nullptr)
        return nullptr;

    void *scene = reinterpret_cast<CreateScene_t>(Offsets::FUN_M2_CREATE_SCENE)();
    if (scene == nullptr)
        return nullptr;
    void *model = M2::CreateModel(
        scene, Game::Read<const char *>(modelData, Offsets::OFF_CREATUREMODELDATA_MODEL_PATH));
    if (model == nullptr) {
        reinterpret_cast<ReleaseScene_t>(Offsets::FUN_M2SCENE_RELEASE)(scene);
        return nullptr;
    }
    reinterpret_cast<ApplyDisplay_t>(Offsets::FUN_APPLY_CREATURE_DISPLAY)(model, display,
                                                                          modelData);

    // The bake's own texture: 64x64, one mip, ARGB8888, flags (1, 0, 0, 0, 0,
    // 0, 1, 0), "Portrait1" (0x005257D3).
    auto p = std::make_unique<Portrait>();
    p->scene = scene;
    p->model = model;
    p->dressRecord = Model::DisplayInfo::DressRecord(displayID);
    p->texture = Texture::CreateFromPixels("Portrait1", kSize, kSize, p->texels.data(),
                                           Texture::Flags(1, false, false));
    if (p->texture == nullptr) {
        ReleaseModel(*p);
        return nullptr;
    }
    g_loading.push_back(p.get());
    return g_portraits.emplace(displayID, std::move(p)).first->second.get();
}

int __fastcall Script_SetPortraitTextureFromCreatureDisplayID(void *L) {
    void *texture = Game::Lua::ResolveTexture(L, 1);
    if (texture == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: SetPortraitTextureFromCreatureDisplayID(texture, displayID)");
        return 0;
    }
    if (Portrait *p = Acquire(static_cast<uint32_t>(Game::Lua::ToNumber(L, 2))))
        reinterpret_cast<SetImage_t>(Offsets::FUN_SIMPLETEXTURE_SET_IMAGE)(texture, p->texture);
    return 0;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("textureObject", "SimpleTexture"),
    Game::Doc::Req("creatureDisplayID", "number", "A CreatureDisplayInfo ID."),
};
const Game::Doc::Function kDoc{
    "Shows a creature's portrait in a texture, by its display ID.", kArgs, {}, "UnitGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("SetPortraitTextureFromCreatureDisplayID",
                                      &Script_SetPortraitTextureFromCreatureDisplayID, &kDoc);
}

const Game::ModuleAutoRegister _register{&RegisterLuaFunctions};

// Leaving the world: drop every portrait, as ShutdownGame drops the engine's.
// The world UI and every region holding one of these textures are gone by
// the time the glue state boots.
void ClearOnLogout() {
    for (Portrait *p : g_loading)
        ReleaseModel(*p);
    g_loading.clear();
    for (auto &entry : g_portraits)
        Texture::ReleaseHandle(entry.second->texture);
    g_portraits.clear();
}

const Game::GlueModuleAutoRegister _clearOnLogout{&ClearOnLogout};

} // namespace
} // namespace Model::Portrait
