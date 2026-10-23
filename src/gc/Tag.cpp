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

#include "gc/Tag.h"

#include "Game.h"
#include "Offsets.h"
#include "gc/Hooks.h"

#include <ankerl/unordered_dense.h>

#include <vector>

namespace Gc::Tag {

namespace {

// Live tagged objects → tag. Untagged objects are never stored, and every
// free erases, so a reused address can't inherit a dead object's tag.
ankerl::unordered_dense::map<const void *, const char *> g_tags;

// `GCUsage` list: tag → bytes at the last UpdateUsage. Like 3.3.5's list, a
// tag's entry survives once seen and is zeroed, not dropped, on a recount.
ankerl::unordered_dense::map<const char *, uint64_t> g_usage;

// `lua_gctag`, innermost last.
std::vector<const char *> g_pins;

void OnCreate(const void *object) {
    const char *ctx = Game::Read<const char *>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT);
    if (ctx == nullptr)
        return;
    const char *pin = g_pins.empty() ? nullptr : g_pins.back();
    g_tags[object] = pin != nullptr ? pin : ctx;
}

void OnFree(const void *object) { g_tags.erase(object); }

const Gc::Hooks::AutoSubscribe _gc{&OnCreate, &OnFree};

uint32_t U8(const void *o, uintptr_t off) { return Game::Read<uint8_t>(o, off); }
uint32_t U16(const void *o, uintptr_t off) { return Game::Read<uint16_t>(o, off); }
uint32_t U32(const void *o, uintptr_t off) { return Game::Read<uint32_t>(o, off); }

} // namespace

const char *Of(const void *object) {
    const auto it = g_tags.find(object);
    return it == g_tags.end() ? nullptr : it->second;
}

uint32_t SizeOf(const void *o) {
    constexpr uint32_t kTValue = Offsets::SIZEOF_LUA_TVALUE;
    switch (U8(o, Offsets::OFF_LUA_GCHEADER_TT)) {
    case Game::Lua::TYPE_STRING:
        return U32(o, Offsets::OFF_LUA_TSTRING_LEN) + Offsets::SIZEOF_LUA_TSTRING_HEADER;
    case Game::Lua::TYPE_TABLE: {
        const uint32_t lsizenode = U8(o, Offsets::OFF_LUA_TABLE_LSIZENODE);
        return Offsets::SIZEOF_LUA_TABLE + kTValue * U32(o, Offsets::OFF_LUA_TABLE_SIZEARRAY) +
               (lsizenode != 0 ? uint32_t{Offsets::SIZEOF_LUA_NODE} << lsizenode : 0);
    }
    case Game::Lua::TYPE_FUNCTION: {
        const uint32_t nup = U8(o, Offsets::OFF_LUA_CLOSURE_NUPVALUES);
        return U8(o, Offsets::OFF_LUA_CLOSURE_ISC) != 0
                   ? (nup + 1) * kTValue
                   : nup * 4 + Offsets::SIZEOF_LUA_LCLOSURE_BASE;
    }
    case Game::Lua::TYPE_USERDATA:
        return U32(o, Offsets::OFF_LUA_UDATA_LEN) + Offsets::SIZEOF_LUA_UDATA_HEADER;
    case Game::Lua::TYPE_THREAD:
        return Offsets::SIZEOF_LUA_STATE + kTValue * U32(o, Offsets::OFF_LUA_STATE_STACKSIZE) +
               Offsets::SIZEOF_LUA_CALLINFO * U16(o, Offsets::OFF_LUA_STATE_SIZE_CI);
    case Offsets::LUA_TT_PROTO:
        return Offsets::SIZEOF_LUA_PROTO + 4 * U32(o, Offsets::OFF_LUA_PROTO_SIZECODE) +
               4 * U32(o, Offsets::OFF_LUA_PROTO_SIZEP) +
               kTValue * U32(o, Offsets::OFF_LUA_PROTO_SIZEK) +
               4 * U32(o, Offsets::OFF_LUA_PROTO_SIZELINEINFO) +
               Offsets::SIZEOF_LUA_LOCVAR * U32(o, Offsets::OFF_LUA_PROTO_SIZELOCVARS) +
               4 * U32(o, Offsets::OFF_LUA_PROTO_SIZEUPVALUES);
    case Offsets::LUA_TT_UPVAL:
        return Offsets::SIZEOF_LUA_UPVAL;
    default:
        return 0;
    }
}

size_t TaggedCount() { return g_tags.size(); }

void PushPin(const char *tag) { g_pins.push_back(tag); }

void PopPin() {
    if (!g_pins.empty())
        g_pins.pop_back();
}

void UpdateUsage() {
    for (auto &entry : g_usage)
        entry.second = 0;
    for (const auto &[object, tag] : g_tags)
        g_usage[tag] += SizeOf(object);
}

uint64_t Usage(const char *tag) {
    const auto it = g_usage.find(tag);
    return it == g_usage.end() ? 0 : it->second;
}

} // namespace Gc::Tag
