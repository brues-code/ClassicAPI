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

// `PlayerModel:SetItem(itemID [, appearanceModID, itemVisualID])`: the pane
// shows one item's own model, the model a unit holds or wears.
//
// A pane shows what its display record names (OFF_PLAYERMODEL_DISPLAY_RECORD).
// SetDisplay stores the record and AttachDisplayModel builds it, at once when
// the pane is visible and again on every show; the SetUnit helper clears it.
// An item is one more kind of record: SetItem passes SetDisplay a record of
// the pane's own, and Model::DisplayInfo's AttachDisplayModel co-hook hands
// that record to Attach below. So hide, show, SetUnit, SetDisplayInfo and
// SetKeepModelOnHide treat an item the way they treat a creature display.
//
// The model is 4.3.4's CGUnit_C::CreateItemModel, built with 1.12's own
// steps:
//   - The directory comes from the inventory type: head, shoulder, shield,
//     ammo, quiver, else weapon (the engine's strings at 0x00838CD8..).
//   - Model name 0, else name 1, with the texture of the same index.
//   - A head item is `<name less its extension>_<race prefix><sex
//     letter>.mdx`, as the helm builder (0x004799A0) makes it. 4.3.4 takes
//     race and sex from the unit that makes the model; a pane has no unit,
//     so they are the player's.
//   - Then AttachItemModelFile's (0x004798C0) steps: the model in the pane's
//     scene, the `%s%s.blp` skin bound as texture type 2, and the item
//     visuals.
//   - Then AttachDisplayModel's (0x00505A70) install: SetModelInstance
//     (vtable +0x90), the pane's done callback, Stand, and the release of the
//     creation reference.
//
// The glow is the display's own ItemVisuals row, else itemVisualID: the rule
// GetItemDescVisual applies to enchants, which TryOn uses too.
// appearanceModID has no meaning without item appearance mods.
//
// An item that is not in the cache is queued with Item::Data::WarmCache, and
// the pane shows nothing until it loads. Then the pane is built again if it
// still shows that item and is visible.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "item/Data.h"
#include "item/Record.h"
#include "model/DisplayInfo.h"
#include "model/ItemDisplay.h"
#include "model/M2.h"
#include "texture/Load.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace Model::ItemDisplay {
namespace {

using PaneRecordFn_t = void(__thiscall *)(void *pane, const uint8_t *record);
using TextureLoad_t = void *(__fastcall *)(const char *path, void *status, uint32_t flags,
                                           int arg4, int arg5);
using ReplaceTexture_t = void(__thiscall *)(void *model, uint32_t type, void *texture);

// The texture type an item model takes its skin as.
constexpr uint32_t kItemSkinTexture = 2;
// AttachItemModelFile's texture filter.
constexpr uint32_t kItemTextureFilter = 3;
constexpr int kSexLetters = 3;
constexpr size_t kPathSize = 260;

// The record a pane shows for SetItem; its display ID stays 0.
struct ItemRecord {
    Pane::DisplayRecord display;
    uint32_t itemID = 0;
    uint32_t visual = 0;
};
std::unordered_map<void *, ItemRecord> g_records;

const char *Directory(uint32_t inventoryType) {
    switch (inventoryType) {
    case Offsets::INVTYPE_HEAD:
        return "Item\\ObjectComponents\\Head\\";
    case Offsets::INVTYPE_SHOULDER:
        return "Item\\ObjectComponents\\Shoulder\\";
    case Offsets::INVTYPE_SHIELD:
        return "Item\\ObjectComponents\\Shield\\";
    case Offsets::INVTYPE_AMMO:
        return "Item\\ObjectComponents\\Ammo\\";
    case Offsets::INVTYPE_QUIVER:
        return "Item\\ObjectComponents\\Quiver\\";
    default:
        return "Item\\ObjectComponents\\Weapon\\";
    }
}

bool Empty(const char *s) {
    return s == nullptr || s[0] == '\0';
}

const char *DisplayString(const uint8_t *display, int base, int index) {
    return Game::Read<const char *>(display, base + index * static_cast<int>(sizeof(uint32_t)));
}

// The helm builder's path: the name cut at its last dot, then the player's
// race prefix and sex letter.
bool HeadPath(char *path, const char *dir, const char *name) {
    const uint8_t sex = Game::Read<uint8_t>(Offsets::VAR_PLAYER_SEX_BYTE);
    const uint8_t *race =
        DBC::Record(Offsets::VAR_CHRRACES_RECORDS, Offsets::VAR_CHRRACES_COUNT,
                    Game::Read<uint8_t>(Offsets::VAR_PLAYER_RACE_BYTE));
    if (race == nullptr || sex >= kSexLetters)
        return false;
    char base[kPathSize];
    std::snprintf(base, sizeof(base), "%s", name);
    if (char *dot = std::strrchr(base, '.'))
        *dot = '\0';
    std::snprintf(path, kPathSize, "%s%s_%s%s.mdx", dir, base,
                  Game::Read<const char *>(race, Offsets::OFF_CHRRACES_CLIENT_PREFIX),
                  Game::Read<const char *>(Offsets::VAR_SEX_LETTERS + sex * sizeof(uint32_t)));
    return true;
}

// 4.3.4's CreateItemModel: a new model the caller owns one reference on, or
// null when the item is not cached or has no model.
void *CreateItemModel(void *pane, const ItemRecord &rec) {
    const uint8_t *item = ::Item::PeekRecord(rec.itemID);
    if (item == nullptr)
        return nullptr;
    const uint8_t *display =
        DBC::Record(Offsets::VAR_ITEMDISPLAYINFO_RECORDS, Offsets::VAR_ITEMDISPLAYINFO_COUNT,
                    Game::Read<uint32_t>(item, Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID));
    if (display == nullptr)
        return nullptr;
    int index = 0;
    if (Empty(DisplayString(display, Offsets::OFF_ITEMDISPLAYINFO_MODEL_NAME, 0))) {
        if (Empty(DisplayString(display, Offsets::OFF_ITEMDISPLAYINFO_MODEL_NAME, 1)))
            return nullptr;
        index = 1;
    }
    const char *name = DisplayString(display, Offsets::OFF_ITEMDISPLAYINFO_MODEL_NAME, index);
    const uint32_t inventoryType = Game::Read<uint32_t>(item, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);
    const char *dir = Directory(inventoryType);

    char path[kPathSize];
    if (inventoryType == Offsets::INVTYPE_HEAD) {
        if (!HeadPath(path, dir, name))
            return nullptr;
    } else {
        std::snprintf(path, sizeof(path), "%s%s", dir, name);
    }
    void *model = M2::CreateModel(Pane::Scene(pane), path);
    if (model == nullptr)
        return nullptr;

    const char *texture =
        DisplayString(display, Offsets::OFF_ITEMDISPLAYINFO_MODEL_TEXTURE, index);
    if (!Empty(texture)) {
        std::snprintf(path, sizeof(path), "%s%s.blp", dir, texture);
        void *handle = reinterpret_cast<TextureLoad_t>(Offsets::FUN_TEXTURE_LOAD_BY_PATH)(
            path, reinterpret_cast<void *>(Offsets::VAR_ITEM_MODEL_TEXTURE_STATUS),
            Texture::Flags(kItemTextureFilter, false, false), 1, 0);
        if (handle == nullptr) {
            M2::Release(model);
            return nullptr;
        }
        reinterpret_cast<ReplaceTexture_t>(Offsets::FUN_M2MODEL_REPLACE_TEXTURE)(
            model, kItemSkinTexture, handle);
        Texture::ReleaseHandle(handle);
    }

    const uint32_t own = OwnVisual(display);
    const uint32_t visual = own != 0 ? own : rec.visual;
    if (visual != 0) {
        M2::AttachItemVisuals(model, visual);
        Model::DisplayInfo::LightChildren(pane, model);
    }
    return model;
}

// AttachDisplayModel's install, with the item's model. A pane with no model
// for the item shows nothing.
void Show(void *pane, const ItemRecord &rec) {
    void *model = CreateItemModel(pane, rec);
    Pane::SetModel(pane, model);
    if (model == nullptr)
        return;
    const auto user = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pane));
    M2::SetAnimDoneCallback(model,
                            reinterpret_cast<const void *>(Offsets::FUN_PLAYERMODEL_ON_ANIM_DONE),
                            user, static_cast<int32_t>(user) < 0 ? 0xFFFFFFFFu : 0u);
    M2::PlayAnimation(model, -1, 0, -1, 0, 1.0f, 1, 1);
    M2::Release(model);
}

// A queued item has loaded: build each visible pane that still shows it,
// through AttachDisplayModel as a show would.
void OnItemLoaded(uint32_t itemID, bool success) {
    if (!success)
        return;
    for (auto &[pane, rec] : g_records)
        if (rec.itemID == itemID && Pane::DisplayRecordOf(pane) == rec.display.bytes &&
            Pane::IsVisible(pane))
            reinterpret_cast<PaneRecordFn_t>(Offsets::FUN_PLAYERMODEL_ATTACH_DISPLAY_MODEL)(
                pane, rec.display.bytes);
}

const ::Item::Data::LoadSubscriber _loadSub{&OnItemLoaded};

int __fastcall Script_SetItem(void *L) {
    void *pane = Game::Lua::ResolvePlayerModel(L);
    if (pane == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: SetItem(itemID [, appearanceModID, itemVisualID])");
        return 0;
    }
    ItemRecord &rec = g_records[pane];
    rec = ItemRecord{};
    rec.itemID = static_cast<uint32_t>(Game::Lua::ToNumber(L, 2));
    if (Game::Lua::IsNumber(L, 4))
        rec.visual = static_cast<uint32_t>(Game::Lua::ToNumber(L, 4));
    Pane::SetDisplay(pane, rec.display.bytes);
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetItem", &Script_SetItem},
};

const Game::Doc::Field kSetItemArgs[] = {
    Game::Doc::Req("itemID", "number", "The item to show."),
    Game::Doc::Opt("appearanceModID", "number", nullptr, "Has no effect."),
    Game::Doc::Opt("itemVisualID", "number", nullptr,
                   "An ItemVisuals ID to show on the item when the item has no glow of its own."),
};
const Game::Doc::Function kSetItem{"Shows the model of an item on the model.", kSetItemArgs, {}};
const Game::Doc::Method g_methodDocs[] = {
    {"SetItem", &kSetItem},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_PLAYERMODEL_METHOD_REGISTRY), g_methods,
        g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// The reload tears every pane down; a later load must not walk them.
void PrepareForReload() {
    g_records.clear();
}

const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

} // namespace

uint32_t OwnVisual(const uint8_t *display) {
    const uint32_t own = Game::Read<uint32_t>(display, Offsets::OFF_ITEMDISPLAYINFO_ITEM_VISUAL);
    return DBC::Record(Offsets::VAR_ITEMVISUALS_RECORDS, Offsets::VAR_ITEMVISUALS_COUNT, own) !=
                   nullptr
               ? own
               : 0;
}

bool Attach(void *pane, const uint8_t *record) {
    const auto it = g_records.find(pane);
    if (record == nullptr || it == g_records.end() || record != it->second.display.bytes)
        return false;
    if (::Item::PeekRecord(it->second.itemID) == nullptr)
        ::Item::Data::WarmCache(it->second.itemID);
    Show(pane, it->second);
    return true;
}

} // namespace Model::ItemDisplay
