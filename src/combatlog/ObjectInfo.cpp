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

// The game objects and the spells' ground areas in view, for replay tools:
// the movement file's GAMEOBJECT_INFO, DYNAMICOBJECT_INFO and OBJECT_GONE
// lines (Movement.cpp documents them). A ClassicAPI extension. Each frame
// walks the objects in view, as UnitInfo walks the units, and writes the line
// of an object new to view or changed since its last line, and OBJECT_GONE for
// one no longer in view.
//
// A game object's fields are the ones the client places and shows it by: its
// model is the display's, its world matrix the translation by its position
// (the object's GetPosition virtual), the rotation by GAMEOBJECT_ROTATION and
// the scale by OBJECT_FIELD_SCALE_X (0x005F7910); GAMEOBJECT_STATE picks a
// door's or chest's open or closed look (the state watcher 0x005F89E0).
// GAMEOBJECT_FACING is not part of the matrix. A transport's position moves
// with its path, but only the other fields' changes write a line.
//
// A dynamic object is the ground area a spell such as Blizzard leaves: the
// client draws the model its spell's visual names, sized to its radius
// (CGDynamicObject_C slot 10, 0x005D5890). Its fields never change.

#include "Game.h"
#include "Offsets.h"
#include "combatlog/File.h"
#include "combatlog/Movement.h"
#include "guid/Guid.h"
#include "object/Resolve.h"
#include "tick/FrameTick.h"
#include "unit/Position.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

namespace CombatLog::ObjectInfo {

namespace {

using File::AppendFixed;
using File::AppendHex;
using File::AppendUnsigned;

// A game object's GAMEOBJECT_INFO fields apart from its place and name.
// Plain data, so two compare with memcmp.
struct Look {
    uint64_t owner;
    uint32_t display;
    uint32_t type;
    uint32_t state;
    uint32_t flags;
    float rotation[4];
    float scale;
    uint32_t pad; // keeps the struct free of padding bytes for memcmp
};

struct Seen {
    Look look;
    std::string name;
    uint32_t frame; // the last walk that found it
};

std::unordered_map<uint64_t, Seen> g_seen;
uint32_t g_frame = 0;
uint32_t g_generation = 0; // the movement file's, when g_seen was last emptied

Look ReadLook(const void *object, const uint8_t *fields) {
    Look look{};
    look.owner = Game::Read<uint64_t>(fields, Offsets::OFF_GAMEOBJECT_FIELD_CREATED_BY);
    look.display = Game::Read<uint32_t>(fields, Offsets::OFF_GAMEOBJECT_FIELD_DISPLAYID);
    look.type = Game::Read<uint32_t>(fields, Offsets::OFF_GAMEOBJECT_FIELD_TYPE_ID);
    look.state = Game::Read<uint32_t>(fields, Offsets::OFF_GAMEOBJECT_FIELD_STATE);
    look.flags = Game::Read<uint32_t>(fields, Offsets::OFF_GAMEOBJECT_FIELD_FLAGS);
    for (int i = 0; i < 4; ++i)
        look.rotation[i] = Game::Read<float>(fields, Offsets::OFF_GAMEOBJECT_FIELD_ROTATION + i * 4);
    look.scale = Game::Read<float>(::Object::Data(object), Offsets::OFF_OBJECT_FIELD_SCALE_X);
    return look;
}

// The name from the object's record, "" until the record arrives.
std::string ReadName(void *object) {
    using GetName_t = const char *(__fastcall *)(void *object);
    const char *name = reinterpret_cast<GetName_t>(Offsets::FUN_GAMEOBJECT_GET_NAME)(object);
    return name != nullptr ? name : "";
}

void AppendPlace(std::string &line, void *object) {
    float position[3] = {};
    ::Unit::Position::Read(object, position);
    for (const float v : position)
        AppendFixed(line, v, 2);
    AppendFixed(line, ::Unit::Position::Facing(object), 4);
}

void WriteGameObject(uint64_t guid, void *object, const Look &look, const std::string &name) {
    std::string line = "GAMEOBJECT_INFO";
    File::AppendGuid(line, guid);
    File::AppendGuid(line, look.owner);
    AppendUnsigned(line, look.display);
    AppendUnsigned(line, look.type);
    AppendUnsigned(line, look.state);
    AppendHex(line, look.flags);
    AppendPlace(line, object);
    for (const float v : look.rotation)
        AppendFixed(line, v, 6);
    AppendFixed(line, look.scale, 4);
    File::AppendQuoted(line, name.c_str());
    Movement::Emit(line);
}

void VisitGameObject(uint64_t guid) {
    void *object = ::Object::ByGuid(Offsets::TYPEMASK_GAMEOBJECT, guid, nullptr, 0);
    const uint8_t *fields = ::Object::Fields(object);
    if (fields == nullptr || ::Object::Data(object) == nullptr)
        return;
    const Look look = ReadLook(object, fields);
    const std::string name = ReadName(object);
    const auto [it, added] = g_seen.try_emplace(guid);
    Seen &seen = it->second;
    seen.frame = g_frame;
    if (added || std::memcmp(&seen.look, &look, sizeof look) != 0 || name != seen.name) {
        seen.look = look;
        seen.name = name;
        WriteGameObject(guid, object, look, name);
    }
}

void VisitDynamicObject(uint64_t guid) {
    void *object = ::Object::ByGuid(Offsets::TYPEMASK_DYNAMICOBJECT, guid, nullptr, 0);
    const uint8_t *fields = ::Object::Fields(object);
    if (fields == nullptr)
        return;
    const auto [it, added] = g_seen.try_emplace(guid);
    it->second.frame = g_frame;
    if (!added)
        return;
    std::string line = "DYNAMICOBJECT_INFO";
    File::AppendGuid(line, guid);
    File::AppendGuid(line, Game::Read<uint64_t>(fields, Offsets::OFF_DYNAMICOBJECT_FIELD_CASTER));
    AppendUnsigned(line, Game::Read<uint32_t>(fields, Offsets::OFF_DYNAMICOBJECT_FIELD_SPELLID));
    AppendHex(line, Game::Read<uint32_t>(fields, Offsets::OFF_DYNAMICOBJECT_FIELD_BYTES));
    AppendFixed(line, Game::Read<float>(fields, Offsets::OFF_DYNAMICOBJECT_FIELD_RADIUS), 2);
    AppendPlace(line, object);
    Movement::Emit(line);
}

void OnFrame() {
    if (!Movement::IsOpen()) {
        g_seen.clear();
        return;
    }
    // Out of world the walk finds nothing, which is not every object leaving.
    if (!::Object::RegistryReady())
        return;
    if (Movement::Generation() != g_generation) {
        g_generation = Movement::Generation();
        g_seen.clear();
    }
    ++g_frame;
    ::Object::ForEachVisible([](uint64_t guid) {
        if (Guid::IsGameObject(guid))
            VisitGameObject(guid);
        else if (Guid::IsDynamicObject(guid))
            VisitDynamicObject(guid);
        return true;
    });
    for (auto it = g_seen.begin(); it != g_seen.end();) {
        if (it->second.frame == g_frame) {
            ++it;
            continue;
        }
        std::string line = "OBJECT_GONE";
        File::AppendGuid(line, it->first);
        Movement::Emit(line);
        it = g_seen.erase(it);
    }
}

const Tick::FrameTick::AutoSubscribe _frame{&OnFrame};

} // namespace

} // namespace CombatLog::ObjectInfo
