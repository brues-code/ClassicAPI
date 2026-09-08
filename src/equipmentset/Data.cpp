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

#include "Data.h"

#include "Offsets.h"
#include "Storage.h"
#include "event/Custom.h"
#include "item/ID.h"
#include "item/Location.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstring>

namespace EquipmentSet::Data {

namespace {

std::vector<Set> g_sets;
std::string g_path;
bool g_loaded = false;
bool g_ignoredForSave[SLOT_COUNT] = {};

// Reads the GUID currently in paperdoll slot `slot1Based` (1..19) by
// indexing the player invMgr's flat GUID array. Slot 1 (HEAD) lives
// at linear index 0; the equipment paperdoll occupies indices 0..18.
// Returns 0 for empty slots or unresolved player.
uint64_t ReadEquippedGUID(int slot1Based) {
    const uint8_t *invMgr = Unit::Identity::PlayerInventoryManager();
    if (invMgr == nullptr)
        return 0;
    auto *guids = *reinterpret_cast<uint64_t *const *>(
        invMgr + Offsets::OFF_INVMGR_GUID_ARRAY);
    if (guids == nullptr)
        return 0;
    return guids[slot1Based - 1];
}

void PopulateFromEquipped(Set *set) {
    for (int i = 0; i < SLOT_COUNT; ++i) {
        if (g_ignoredForSave[i]) {
            set->items[i] = GUID_IGNORED;
            set->itemIDs[i] = 0;
            continue;
        }
        set->items[i] = ReadEquippedGUID(i + 1);
        // Capture itemID alongside GUID so the tooltip's missing-items
        // path can resolve names even after the item leaves inventory.
        // Resolves to the same CGItem the engine sees in the paperdoll
        // slot; ID is 0 for empty slots.
        const uint8_t *item = Item::Location::ResolveEquipmentSlot(i + 1);
        set->itemIDs[i] = (item != nullptr)
            ? static_cast<uint32_t>(Item::ID::FromCGItem(item))
            : 0;
    }
}

uint32_t NextAvailableID() {
    uint32_t maxID = 0;
    for (const auto &s : g_sets)
        if (s.setID > maxID)
            maxID = s.setID;
    return maxID + 1;
}

void FireChanged() {
    const int slot = kEvtSetsChanged.Slot();
    if (slot >= 0)
        Event::Custom::Fire(slot, "");
}

void Persist() {
    if (!g_path.empty())
        Storage::Save(g_path, g_sets);
}

} // namespace

void EnsureLoaded() {
    // Re-resolve the per-character path on every call rather than latching
    // once. The injected DLL persists across a logout→character-select→login
    // (no process restart), so a permanent `g_loaded` latch would leave the
    // previous character's file path and cached sets in place — the next
    // character would then read, and save into, character A's data. Detecting
    // a path change here makes the cache self-heal on any character/account
    // switch. Every mutation Persist()s immediately, so dropping the old
    // in-memory sets never loses unsaved work.
    std::string path = Storage::ResolveFilePath();
    if (path.empty())
        return; // session globals not yet populated; keep current, retry later
    if (g_loaded && path == g_path)
        return; // already loaded for this character

    g_path = std::move(path);
    g_sets.clear();
    Storage::Load(g_path, &g_sets);
    // Per-session "ignore this slot on next save" state belongs to the
    // character it was set on — clear it when switching characters.
    for (int i = 0; i < SLOT_COUNT; ++i)
        g_ignoredForSave[i] = false;
    g_loaded = true;
}

const std::vector<Set> &All() {
    EnsureLoaded();
    return g_sets;
}

const Set *FindByID(uint32_t setID) {
    EnsureLoaded();
    for (const auto &s : g_sets)
        if (s.setID == setID)
            return &s;
    return nullptr;
}

const Set *FindByName(const char *name) {
    if (name == nullptr)
        return nullptr;
    EnsureLoaded();
    for (const auto &s : g_sets)
        if (s.name == name)
            return &s;
    return nullptr;
}

int IndexOf(uint32_t setID) {
    EnsureLoaded();
    for (size_t i = 0; i < g_sets.size(); ++i)
        if (g_sets[i].setID == setID)
            return static_cast<int>(i);
    return -1;
}

std::vector<const Set *> SetsContainingItem(uint64_t itemGuid) {
    EnsureLoaded();
    std::vector<const Set *> found;
    // The sentinels are slot markers, not items — a set with an empty or
    // ignored slot must not match a caller that hands us one of them.
    if (itemGuid == GUID_EMPTY || itemGuid == GUID_IGNORED)
        return found;

    for (const Set &s : g_sets) {
        for (int i = 0; i < SLOT_COUNT; ++i) {
            if (s.items[i] == itemGuid) {
                found.push_back(&s);
                break; // one hit per set, however many slots match
            }
        }
    }
    return found;
}

uint32_t Create(const char *name, const char *icon) {
    EnsureLoaded();
    if (g_path.empty() || name == nullptr || name[0] == '\0')
        return 0;
    if (FindByName(name) != nullptr)
        return 0;
    Set s;
    s.setID = NextAvailableID();
    s.name = name;
    if (s.name.size() > MAX_NAME_LEN)
        s.name.resize(MAX_NAME_LEN);
    s.icon = (icon != nullptr && icon[0] != '\0') ? icon : DefaultIcon();
    PopulateFromEquipped(&s);
    g_sets.push_back(std::move(s));
    Persist();
    FireChanged();
    return g_sets.back().setID;
}

bool SaveExisting(uint32_t setID, const char *icon) {
    EnsureLoaded();
    if (g_path.empty())
        return false;
    const int idx = IndexOf(setID);
    if (idx < 0)
        return false;
    Set &s = g_sets[idx];
    if (icon != nullptr && icon[0] != '\0')
        s.icon = icon;
    PopulateFromEquipped(&s);
    Persist();
    FireChanged();
    return true;
}

bool Rename(uint32_t setID, const char *newName) {
    EnsureLoaded();
    if (g_path.empty() || newName == nullptr || newName[0] == '\0')
        return false;
    const int idx = IndexOf(setID);
    if (idx < 0)
        return false;
    // Reject duplicate names that belong to a different set.
    const Set *clash = FindByName(newName);
    if (clash != nullptr && clash->setID != setID)
        return false;
    g_sets[idx].name = newName;
    if (g_sets[idx].name.size() > MAX_NAME_LEN)
        g_sets[idx].name.resize(MAX_NAME_LEN);
    Persist();
    FireChanged();
    return true;
}

bool Delete(uint32_t setID) {
    EnsureLoaded();
    if (g_path.empty())
        return false;
    const int idx = IndexOf(setID);
    if (idx < 0)
        return false;
    g_sets.erase(g_sets.begin() + idx);
    Persist();
    FireChanged();
    return true;
}

void IgnoreSlot(int slot1Based) {
    if (slot1Based < 1 || slot1Based > SLOT_COUNT)
        return;
    g_ignoredForSave[slot1Based - 1] = true;
    FireChanged();
}

void UnignoreSlot(int slot1Based) {
    if (slot1Based < 1 || slot1Based > SLOT_COUNT)
        return;
    g_ignoredForSave[slot1Based - 1] = false;
    FireChanged();
}

void ClearIgnoredSlots() {
    for (int i = 0; i < SLOT_COUNT; ++i)
        g_ignoredForSave[i] = false;
    FireChanged();
}

bool IsSlotIgnored(int slot1Based) {
    if (slot1Based < 1 || slot1Based > SLOT_COUNT)
        return false;
    return g_ignoredForSave[slot1Based - 1];
}

} // namespace EquipmentSet::Data
