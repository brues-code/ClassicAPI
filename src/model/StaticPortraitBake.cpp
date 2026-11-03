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

// Static creature portraits through the original 1.12.1 M2 scene and
// portrait camera. The client renders a 64x64 image from a temporary model
// and the result is retained by display ID for subsequent texture requests.
// Engine entry points are centralized in Offsets.h (build 5875).
// Uses ClassicAPI's existing PlayerModel Lua methods and shared tick/region
// observers; no embedded Lua worker or custom hooks.

#include "Game.h"
#include "Offsets.h"
#include "model/DisplayInfo.h"
#include "frame/RegionDestruction.h"
#include "texture/Load.h"
#include "tick/FrameTick.h"

#include <array>
#include <deque>
#include <cmath>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace Model::StaticPortraitBake {
namespace {

constexpr uint32_t kSize = 64;
constexpr uint32_t kPixels = kSize * kSize;

using CreateScene_t = void *(__fastcall*)();
using ReleaseScene_t = void (__fastcall*)(void *);
using DuplicateModel_t = void *(__thiscall*)(void *scene, void *model, uint32_t flags);
using ReleaseModel_t = void (__fastcall*)(void *);
using IsDrawable_t = int (__thiscall*)(void *, int, int);
using SetAnimated_t = void (__thiscall*)(void *, int);
using SetVisible_t = void (__thiscall*)(void *, int);
using PlayAnimation_t = void (__thiscall*)(void *, int, int, int, int, float, int, int);
using GetCamera_t = void *(__thiscall*)(void *, uint32_t);
using CameraProjection_t = void (__fastcall*)(void *camera, const float *rect, int unused);
using AnimateScene_t = void (__thiscall*)(void *, const float *cameraPosition);
using DrawScene_t = void (__thiscall*)(void *, uint32_t pass);
using RemoveVisuals_t = void (__fastcall*)(void *);
using SetRenderCallback_t = void (__thiscall*)(void *, const void *, void *);
using GetCameraVector_t = void (__fastcall*)(void *camera, uint32_t id, float *out);
using GetMatrix_t = void (__fastcall*)(float *);
using SetMatrix_t = void (__fastcall*)(const float *);
using GetViewport_t = void (__fastcall*)(float *, float *, float *, float *, float *, float *);
using SetViewport_t = void (__stdcall*)(float, float, float, float, float, float);
using GetScreenSize_t = void (__fastcall*)(float *);
using DdcToNdc_t = void (__fastcall*)(float *, float *, float, float);
using Clear_t = void (__fastcall*)(int);
using BindTexture_t = void (__thiscall*)(void *, void *);
using GetMask_t = void *(__fastcall*)(uint32_t);
using SetCapability_t = void (__thiscall*)(void *, uint32_t bit, int enabled);

// The 1.12 GrowableArray stores capacity, count, data and growth increment
// at +0,+4,+8,+C (see benilla-re charactermodel/container). The device
// ReadPixels vmethod may call SetCount; supplying a capacity of 4096 avoids
// allocating storage inside Storm. No destructor runs on this stack view.
struct PixelArray {
    uint32_t capacity;
    uint32_t count;
    uint32_t *data;
    uint32_t increment;
};
#if defined(_WIN32)
static_assert(sizeof(PixelArray) == 16, "Target must be Win32");
#endif
struct RectI { int32_t top, left, bottom, right; };

struct Entry {
    std::array<uint32_t, kPixels> pixels{}; // texture upload source; held with cache
    void *handle = nullptr; // native texture reference, retained with source
};
std::unordered_map<uint32_t, std::unique_ptr<Entry>> g_cache;
// The cache is keyed only by display ID, so it survives /reload. Keep each
// pixel buffer alive while the native texture's upload callback may read it;
// unlike a separate retired list, this stores one buffer per display ID.

bool Bind(void *destination, Entry &entry) {
    if (destination == nullptr || entry.handle == nullptr)
        return false;
    reinterpret_cast<BindTexture_t>(Offsets::FUN_PORTRAIT_SET_TEXTURE_HANDLE)(destination, entry.handle);
    return true;
}

bool MaskPixels(Entry &entry) {
    // Native GetAlphaMask(64) returns a TSFixedArray view. If its ABI does not
    // match, do not read an arbitrary pointer: return false to the caller.
    void *raw = reinterpret_cast<GetMask_t>(Offsets::FUN_PORTRAIT_ALPHA_MASK)(kSize);
    if (raw == nullptr)
        return false;
    const auto *mask = static_cast<const PixelArray *>(raw);
    if (mask->count < kPixels || mask->data == nullptr)
        return false;
    const auto *alpha = reinterpret_cast<const uint8_t *>(mask->data);
    for (uint32_t i = 0; i < kPixels; ++i)
        entry.pixels[i] = (entry.pixels[i] & 0x00FFFFFFu) | (static_cast<uint32_t>(alpha[i]) << 24);
    return true;
}

struct SavedTransforms {
    float projection[16]{};
    float view[16]{};
    float vp[6]{};
    void *device = Game::Read<void *>(Offsets::VAR_GX_DEVICE);
    bool fog = false;

    void SetFog(bool enabled) const {
        void *vtbl = Game::Read<void *>(device, 0);
        auto set = Game::Read<SetCapability_t>(vtbl, Offsets::VTBL_GXDEVICE_SET_CAP);
        set(device, Offsets::GX_CAP_FOG, enabled ? 1 : 0);
    }

    SavedTransforms() {
        reinterpret_cast<GetMatrix_t>(Offsets::FUN_GX_GET_PROJECTION)(projection);
        reinterpret_cast<GetMatrix_t>(Offsets::FUN_GX_GET_VIEW)(view);
        reinterpret_cast<GetViewport_t>(Offsets::FUN_GX_GET_VIEWPORT)(
            &vp[0], &vp[1], &vp[2], &vp[3], &vp[4], &vp[5]);
        fog = (Game::Read<uint32_t>(device, Offsets::OFF_GXDEV_CAPABILITY_BITS) &
               (1u << Offsets::GX_CAP_FOG)) != 0;
        SetFog(false);
    }
    ~SavedTransforms() {
        reinterpret_cast<SetMatrix_t>(Offsets::FUN_GX_SET_PROJECTION)(projection);
        reinterpret_cast<SetMatrix_t>(Offsets::FUN_GX_SET_VIEW)(view);
        reinterpret_cast<SetViewport_t>(Offsets::FUN_GX_SET_VIEWPORT)(
            vp[0], vp[1], vp[2], vp[3], vp[4], vp[5]);
        SetFog(fog);
    }
};

bool ReadPixels(Entry &entry) {
    // Device vtable slot +0x48, Benilla gx/slots: ReadPixels(rect,array).
    // Original renderer reads the 64x64 corner of the back buffer.
    void *device = Game::Read<void *>(Offsets::VAR_GX_DEVICE);
    if (device == nullptr)
        return false;
    void *vtable = Game::Read<void *>(device, 0);
    if (vtable == nullptr)
        return false;
    using DeviceRead_t = void (__thiscall*)(void *, const RectI*, PixelArray *);
    auto fn = Game::Read<DeviceRead_t>(vtable, Offsets::VTBL_GXDEVICE_READ_PIXELS);
    if (fn == nullptr)
        return false;
    // Use the original readback path. No pixel-colour heuristic: Vanilla
    // accepts a portrait based on model readiness and the native GX draw.
    PixelArray array{kPixels, kPixels, entry.pixels.data(), kPixels};
    RectI rect{0, 0, static_cast<int32_t>(kSize), static_cast<int32_t>(kSize)};
    fn(device, &rect, &array);
    return array.data == entry.pixels.data() && array.count == kPixels;
}

// Draw the two passes from the M2 scene's existing collection.
void DrawCopy(void *scene, void *model) {
    reinterpret_cast<Clear_t>(Offsets::FUN_GX_SCENE_CLEAR)(3);
    reinterpret_cast<IsDrawable_t>(Offsets::FUN_PORTRAIT_MODEL_IS_DRAWABLE)(model, 1, 1);
    reinterpret_cast<DrawScene_t>(Offsets::FUN_PORTRAIT_SCENE_DRAW)(scene, 0);
    reinterpret_cast<DrawScene_t>(Offsets::FUN_PORTRAIT_SCENE_DRAW)(scene, 1);
}

// This operates on a duplicate, leaving the source PlayerModel unchanged.
// The native renderer must be entered in a valid client graphics context.
bool Bake(void *pane, uint32_t displayID) {
    if (pane == nullptr || displayID == 0)
        return false;
    auto cached = g_cache.find(displayID);
    if (cached != g_cache.end())
        return true;
    if (!Model::DisplayInfo::IsPortraitAppearanceReady(pane))
        return false;
    void *source = Game::Read<void *>(pane, Offsets::OFF_SIMPLEMODELFFX_MODEL_INSTANCE);
    if (source == nullptr || Game::Read<void *>(Offsets::VAR_GX_DEVICE) == nullptr)
        return false;
    void *scene = reinterpret_cast<CreateScene_t>(Offsets::FUN_PORTRAIT_SCENE_CREATE)();
    if (scene == nullptr)
        return false;
    void *copy = reinterpret_cast<DuplicateModel_t>(Offsets::FUN_PORTRAIT_SCENE_DUPLICATE)(scene, source, 0);
    if (copy == nullptr) {
        reinterpret_cast<ReleaseScene_t>(Offsets::FUN_PORTRAIT_SCENE_RELEASE)(scene);
        return false;
    }

    bool ok = false;
    do {
        // First collection evaluates the native portrait camera.
        reinterpret_cast<RemoveVisuals_t>(Offsets::FUN_PORTRAIT_REMOVE_VISUALS)(copy);
        reinterpret_cast<SetRenderCallback_t>(Offsets::FUN_MODEL_INSTANCE_SET_RENDER_CB)(
            copy, reinterpret_cast<const void *>(Offsets::FUN_PORTRAIT_LIGHTING_CALLBACK), nullptr);
        reinterpret_cast<PlayAnimation_t>(Offsets::FUN_PORTRAIT_MODEL_PLAY_ANIMATION)(
            copy, -1, 0, 0, 0, 1.0f, 0, 1);
        reinterpret_cast<SetAnimated_t>(Offsets::FUN_PORTRAIT_MODEL_SET_ANIMATING)(copy, 1);
        const float zero[3] = {0.0f, 0.0f, 0.0f};
        reinterpret_cast<AnimateScene_t>(Offsets::FUN_PORTRAIT_SCENE_ANIMATE)(scene, zero);

        // Camera zero is the M2 portrait camera, not the UI model camera.
        const uint32_t cameraCount = reinterpret_cast<uint32_t (__thiscall*)(void *)>(
            Offsets::FUN_M2MODEL_GET_CAMERA_COUNT)(copy);
        if (cameraCount == 0)
            break;
        void *shared = Game::Read<void *>(copy, Offsets::OFF_M2MODEL_SHARED);
        if (shared == nullptr)
            break;
        auto *data = Game::Read<const uint8_t *>(shared, Offsets::OFF_M2SHARED_DATA);
        if (data == nullptr)
            break;
        const uint16_t index = reinterpret_cast<uint16_t (__fastcall*)(uint32_t, const void *)>(
            Offsets::FUN_M2_LOOKUP_INDEX)(0, data + Offsets::OFF_M2DATA_CAMERA_ID_LOOKUP);
        if (index >= cameraCount)
            break;
        void *camera = reinterpret_cast<GetCamera_t>(Offsets::FUN_PORTRAIT_MODEL_CAMERA)(copy, 0);
        if (camera == nullptr)
            break;
        float eye[3]{};
        reinterpret_cast<GetCameraVector_t>(Offsets::FUN_CAMERA_GET_VECTOR)(
            camera, Offsets::CAMERA_PROP_EYE, eye);
        const float eyeSquared = eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2];
        if (!std::isfinite(eyeSquared) || eyeSquared <= 0.0f)
            break;

        float dimensions[4]{};
        reinterpret_cast<GetScreenSize_t>(Offsets::FUN_GX_CAPS_SCREEN_SIZE)(dimensions);
        const float width = dimensions[3] - dimensions[1];
        const float height = dimensions[2] - dimensions[0];
        if (!(width >= kSize && height >= kSize))
            break;
        // Vanilla computes a 64x64 DDC viewport from the live screen size
        // and UI coordinate scales, then uses its actual aspect for projection.
        const float ddcWidth = Game::Read<float>(Offsets::VAR_COORD_DDC_WIDTH_SCALE);
        const float ddcHeight = Game::Read<float>(Offsets::VAR_COORD_DDC_HEIGHT_SCALE);
        const float right = ddcWidth * (static_cast<float>(kSize) / width);
        const float top = ddcHeight - (ddcHeight / height) * static_cast<float>(kSize);
        const float projectionRect[4] = {0.0f, 0.0f, 1.0f, right / (ddcHeight - top)};

        float minX, minY, maxX, maxY;
        auto toNdc = reinterpret_cast<DdcToNdc_t>(Offsets::FUN_COORD_DDC_TO_NDC);
        toNdc(&minX, &minY, 0.0f, top);
        toNdc(&maxX, &maxY, right, ddcHeight);

        auto entry = std::make_unique<Entry>();
        {
            SavedTransforms restore;
            reinterpret_cast<SetViewport_t>(Offsets::FUN_GX_SET_VIEWPORT)(
                minX, maxX, minY, maxY, 0.0f, 1.0f);
            reinterpret_cast<CameraProjection_t>(Offsets::FUN_PORTRAIT_CAMERA_PROJECTION)(camera, projectionRect, 0);
            // Original 1.12 draws only after the second collection.
            reinterpret_cast<Clear_t>(Offsets::FUN_GX_SCENE_CLEAR)(3);
            reinterpret_cast<IsDrawable_t>(Offsets::FUN_PORTRAIT_MODEL_IS_DRAWABLE)(copy, 1, 1);
            // The second collection uses the evaluated portrait camera eye.
            // Visibility, animation and draw flags must precede it.
            reinterpret_cast<SetVisible_t>(Offsets::FUN_PORTRAIT_MODEL_SET_VISIBLE)(copy, 1);
            reinterpret_cast<SetAnimated_t>(Offsets::FUN_PORTRAIT_MODEL_SET_ANIMATING)(copy, 1);
            Game::Ref<uint32_t>(copy, Offsets::OFF_PORTRAIT_MODEL_DRAW_FLAGS) |= 0x00010008u;
            reinterpret_cast<SetRenderCallback_t>(Offsets::FUN_MODEL_INSTANCE_SET_RENDER_CB)(
                copy, reinterpret_cast<const void *>(Offsets::FUN_PORTRAIT_LIGHTING_CALLBACK), nullptr);
            reinterpret_cast<AnimateScene_t>(Offsets::FUN_PORTRAIT_SCENE_ANIMATE)(scene, eye);
            if (Game::Read<uint32_t>(scene, Offsets::OFF_PORTRAIT_SCENE_ELEMENT_COUNT) != 0) {
                DrawCopy(scene, copy);
                ok = ReadPixels(*entry);
            }
            reinterpret_cast<Clear_t>(Offsets::FUN_GX_SCENE_CLEAR)(3);
        }
        if (!ok)
            break;
        ok = false;
        if (!MaskPixels(*entry))
            break;
        entry->handle = Texture::CreateFromPixels("ClassicAPI\\CreaturePortrait", kSize,
            kSize, entry->pixels.data(), Texture::DefaultFlags());
        if (entry->handle == nullptr)
            break;
        g_cache.emplace(displayID, std::move(entry));
        ok = true;
    } while (false);

    reinterpret_cast<ReleaseModel_t>(Offsets::FUN_PORTRAIT_MODEL_RELEASE)(copy);
    reinterpret_cast<ReleaseScene_t>(Offsets::FUN_PORTRAIT_SCENE_RELEASE)(scene);
    return ok;
}

bool HasPortraitCamera(void *instance) {
    if (instance == nullptr)
        return false;
    const uint32_t count = reinterpret_cast<uint32_t (__thiscall*)(void *)>(
        Offsets::FUN_M2MODEL_GET_CAMERA_COUNT)(instance);
    if (count == 0)
        return false;
    const auto *shared = Game::Read<const uint8_t *>(instance, Offsets::OFF_M2MODEL_SHARED);
    if (shared == nullptr)
        return false;
    const auto *data = Game::Read<const uint8_t *>(shared, Offsets::OFF_M2SHARED_DATA);
    if (data == nullptr)
        return false;
    const uint16_t idx = reinterpret_cast<uint16_t (__fastcall*)(uint32_t, const void *)>(
        Offsets::FUN_M2_LOOKUP_INDEX)(0, data + Offsets::OFF_M2DATA_CAMERA_ID_LOOKUP);
    return idx < count;
}

bool ParseID(void *L, int index, uint32_t *out) {
    if (!Game::Lua::IsNumber(L, index))
        return false;
    double raw = Game::Lua::ToNumber(L, index);
    if (!(raw >= 1.0 && raw <= 4294967295.0))
        return false;
    uint32_t id = static_cast<uint32_t>(raw);
    if (static_cast<double>(id) != raw)
        return false;
    *out = id;
    return true;
}

// --- Asynchronous display-ID worker -------------------------------------------

// One real PlayerModel, built by Vanilla's frame factory. Its Lua registry
// reference keeps the frame alive; the native tick drives the request queue.
void *g_worker = nullptr;
int g_workerRef = -2; // LUA_NOREF
uint32_t g_active = 0;
int g_ticks = 0;
int g_attempts = 0;
std::deque<uint32_t> g_pending;
std::unordered_map<void *, uint32_t> g_requests;

// Invoke the engine's existing frame methods through its Lua wrapper.
// Both worker construction and display changes use this path.
template <typename PushArgs>
bool CallMethod(void *L, int object, const char *method, int nargs, PushArgs pushArgs) {
    using namespace Game::Lua;
    const int top = GetTop(L);
    PushString(L, method);
    GetTable(L, object);
    PushValue(L, object);
    pushArgs(L);
    const bool ok = PCall(L, nargs + 1, 0, 0) == 0;
    SetTop(L, top);
    return ok;
}

void PushGlobal(void *L, const char *name) {
    Game::Lua::PushString(L, name);
    Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
}

// Use the original UI frame factory through the existing Lua C bindings.
// The result is a native PlayerModel with its own model scene and callbacks.
bool EnsureWorker(void *L) {
    using namespace Game::Lua;
    if (g_worker != nullptr)
        return true;
    const int top = GetTop(L);
    PushGlobal(L, "CreateFrame");
    PushString(L, "PlayerModel");
    PushNil(L);
    PushGlobal(L, "UIParent");
    if (PCall(L, 3, 1, 0) != 0) {
        SetTop(L, top);
        return false;
    }
    const int model = top + 1;
    void *pane = ResolveTypedObject(L, model, Offsets::VAR_PLAYERMODEL_LUA_TYPE_ID, false);
    if (pane == nullptr) {
        SetTop(L, top);
        return false;
    }
    auto size = [](void *s) { Game::Lua::PushNumber(s, 1.0); };
    auto background = [](void *s) { Game::Lua::PushString(s, "BACKGROUND"); };
    auto point = [](void *s) {
        Game::Lua::PushString(s, "BOTTOMLEFT");
        PushGlobal(s, "UIParent");
        Game::Lua::PushString(s, "BOTTOMLEFT");
        Game::Lua::PushNumber(s, 0.0);
        Game::Lua::PushNumber(s, 0.0);
    };
    const bool ready =
        CallMethod(L, model, "SetWidth", 1, size) &&
        CallMethod(L, model, "SetHeight", 1, size) &&
        CallMethod(L, model, "SetPoint", 5, point) &&
        CallMethod(L, model, "SetFrameStrata", 1, background) &&
        CallMethod(L, model, "Show", 0, [](void *) {});
    if (!ready) {
        SetTop(L, top);
        return false;
    }
    PushValue(L, model);
    g_workerRef = Ref(L, REGISTRY_INDEX);
    g_worker = pane;
    SetTop(L, top);
    return true;
}

// Reuse PlayerModel:SetDisplayInfo, just as a Lua addon would. The worker's
// registry reference provides the frame object when called from the tick.
bool SetWorkerDisplayInfo(uint32_t id) {
    using namespace Game::Lua;
    void *L = State();
    if (L == nullptr || g_workerRef < 0)
        return false;
    const int top = GetTop(L);
    RawGetI(L, REGISTRY_INDEX, g_workerRef);
    bool ok = false;
    if (Type(L, -1) == TYPE_TABLE) {
        ok = CallMethod(L, top + 1, "SetDisplayInfo", 1, [id](void *s) {
            Game::Lua::PushNumber(s, static_cast<double>(id));
        });
    }
    SetTop(L, top);
    return ok;
}

bool Wanted(uint32_t id) {
    for (const auto &request : g_requests)
        if (request.second == id)
            return true;
    return false;
}

void Finish(uint32_t id, bool completed) {
    auto entry = completed ? g_cache.find(id) : g_cache.end();
    for (auto it = g_requests.begin(); it != g_requests.end();) {
        if (it->second != id) {
            ++it;
            continue;
        }
        // RegionDestruction drops destinations when their texture region dies.
        if (entry != g_cache.end())
            Bind(it->first, *entry->second);
        it = g_requests.erase(it);
    }
    g_active = 0;
}

void StartNext() {
    if (g_active != 0 || g_worker == nullptr)
        return;
    while (!g_pending.empty()) {
        const uint32_t id = g_pending.front();
        g_pending.pop_front();
        if (!Wanted(id))
            continue;
        g_active = id;
        g_ticks = g_attempts = 0;
        if (!SetWorkerDisplayInfo(id)) {
            Finish(id, false);
            continue;
        }
        return;
    }
}

// Runs at the start of the UI frame through FrameTick::AutoSubscribeBefore.
void Pump() {
    if (g_worker == nullptr)
        return;
    if (g_active == 0) {
        StartNext();
        return;
    }
    const uint32_t id = g_active;
    if (!Wanted(id)) {
        Finish(id, false);
        StartNext();
        return;
    }
    ++g_ticks;
    if (Model::DisplayInfo::IsPortraitAppearanceReady(g_worker)) {
        void *instance = Game::Read<void *>(g_worker, Offsets::OFF_SIMPLEMODELFFX_MODEL_INSTANCE);
        if (!HasPortraitCamera(instance)) {
            Finish(id, false);
        } else if (Bake(g_worker, id)) {
            Finish(id, true);
        } else if (++g_attempts >= 4) {
            Finish(id, false);
        }
    } else if (g_ticks >= 600) {
        Finish(id, false);
    }
    StartNext();
}

// A texture region may be recycled before its parent frame is destroyed.
// Drop its pending assignment at the shared region destructor, without
// dereferencing the freed texture or introducing another native hook.
void OnRegionDestroyed(void *region) {
    g_requests.erase(region);
}

const Frame::RegionDestruction::AutoSubscribe _regionDestroyed{&OnRegionDestroyed};
// The native baker reads the back buffer. Run before the UI pass, not after
// it, so the next UI draw is never replaced by the portrait's clear/draw.
const Tick::FrameTick::AutoSubscribeBefore _tick{&Pump};

int __fastcall Script_SetPortrait(void *L) {
    using namespace Game::Lua;
    void *texture = ResolveTexture(L, 1);
    if (texture == nullptr)
        return 0;
    uint32_t id = 0;
    if (!ParseID(L, 2, &id)) {
        Error(L, "Usage: SetPortraitTextureFromCreatureDisplayID(texture, displayID)");
        return 0;
    }
    auto cached = g_cache.find(id);
    if (cached != g_cache.end()) {
        g_requests.erase(texture);
        PushBool(L, Bind(texture, *cached->second));
        return 1;
    }
    if (!EnsureWorker(L)) {
        PushBool(L, false);
        return 1;
    }
    g_requests[texture] = id;
    if (g_active != id) {
        bool pending = false;
        for (uint32_t queued : g_pending)
            if (queued == id) {
                pending = true;
                break;
            }
        if (!pending)
            g_pending.push_back(id);
    }
    StartNext();
    PushBool(L, true); // accepted; portrait will be bound asynchronously
    return 1;
}

int __fastcall Script_Cancel(void *L) {
    void *texture = Game::Lua::ResolveTexture(L, 1);
    if (texture != nullptr)
        g_requests.erase(texture);
    return 0;
}

const Game::Doc::Field kPortraitArgs[] = {
    Game::Doc::Req("texture", "SimpleTexture", "Destination texture."),
    Game::Doc::Req("displayID", "number", "Creature display ID.")};
const Game::Doc::Field kPortraitReturn[] = {
    Game::Doc::Req("accepted", "bool", "True if assigned or queued for rendering.")};
const Game::Doc::Function kPortraitDoc{
    "Requests a cached or asynchronously rendered creature portrait by display ID.",
    kPortraitArgs, kPortraitReturn, "TextureGlobals", true};
const Game::Doc::Field kCancelArgs[] = {
    Game::Doc::Req("texture", "SimpleTexture", "Destination texture.")};
const Game::Doc::Function kCancelDoc{
    "Cancels a pending creature portrait assignment for this texture.",
    kCancelArgs, {}, "TextureGlobals", true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("SetPortraitTextureFromCreatureDisplayID",
                                      &Script_SetPortrait, &kPortraitDoc);
    Game::Lua::RegisterGlobalFunction("ClassicAPI_CancelPortraitTextureRequest",
                                      &Script_Cancel, &kCancelDoc);
}

void PrepareForReload() {
    g_requests.clear();
    g_pending.clear();
    g_active = 0;
    g_worker = nullptr;
    if (g_workerRef >= 0)
        Game::Lua::Unref(Game::Lua::State(), Game::Lua::REGISTRY_INDEX, g_workerRef);
    g_workerRef = -2;
}

const Game::ModuleAutoRegister _register{&RegisterLuaFunctions};
const Game::ReloadAutoRegister _reload{&PrepareForReload};
} // namespace
} // namespace Model::StaticPortraitBake