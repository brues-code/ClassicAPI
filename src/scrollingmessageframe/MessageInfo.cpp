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

// `ScrollingMessageFrame:GetMessageInfo(index)`, retail's: the message's
// text, its colour, then every argument AddMessage took after the colour.
//
// Retail's ScrollingMessageFrame is a Lua mixin whose AddMessage(message, r,
// g, b, ...) packs the call into an entry and GetMessageInfo unpacks it
// (Blizzard_SharedXML/ScrollingMessageFrame.lua). Index 1 is the oldest
// message; the extra arguments come back as given, nils and count included;
// an index outside 1..GetNumMessages() returns nothing. (4.3.4's C method
// returns text, accessID, typeID, extraData instead.)
//
// The text and colour come from the engine's own record of the message
// (Offsets.h, OFF_SCROLLINGMESSAGEFRAME_MESSAGES), so a colour
// UpdateColorByID changed reads back changed, as retail's AdjustMessageColors
// does. The record keeps the colour as bytes, so a component reads back as
// byte/255. It keeps the first extra argument only as an integer (0 for
// anything else), so the extra arguments are kept beside the ring instead.
//
// AddMessage is registered again on the ScrollingMessageFrame registry, over
// the engine's. The registrar (0x00701D80) adds a node for the name whatever
// is there; Storm's TSHashTable::InternalNew links a new node at the head of
// its bucket (NewNode(1)), and the method lookup (0x00702000) returns the
// first match, so this one answers. It runs the engine's AddMessage, then
// records the extra arguments for the record the engine wrote. The engine
// adds a message exactly when the text is a nonempty string
// (0x0079299C..0x007929C7: lua_isstring, lua_tostring, first byte); a bad
// `self` raises a Lua error instead. The ring's one writer has no other
// caller, so every record was written through here.
//
// The extra arguments live in a registry table keyed by the frame's Lua table,
// then by record. A record's table is refilled when the record is overwritten,
// so a full ring makes no garbage.
//
// `BackFillMessage(text, r, g, b, ...)`, retail's, adds a message as the
// OLDEST — the combat log's refilter walks its history newest to oldest and
// fills the frame behind the lines it already shows (Blizzard_CombatLog's
// RefilterUpdate). It writes the ring itself; see the function.

#include "Game.h"
#include "Offsets.h"
#include "scrollingmessageframe/DisplayRefreshed.h"

#include <cstdint>
#include <cstring>

namespace ScrollingMessageFrame::MessageInfo {

namespace {

constexpr int kFirstExtra = 6; // self, text, r, g, b, then the extra arguments

// The frame-table → record → extra-arguments table, in the registry of the Lua
// state that made it. LUA_NOREF until first use in each state.
constexpr int kNoRef = -2;
int g_extrasRef = kNoRef;

// Pushes the extra-argument table of record `record` of the frame whose Lua
// table is at stack slot 1, or nil when it has none and `create` is false.
void PushExtras(void *L, int record, bool create) {
    using namespace Game::Lua;
    if (g_extrasRef == kNoRef) {
        if (!create) {
            PushNil(L);
            return;
        }
        NewTable(L);
        g_extrasRef = Ref(L, REGISTRY_INDEX);
    }
    RawGetI(L, REGISTRY_INDEX, g_extrasRef); // [frames]
    PushValue(L, 1);
    RawGet(L, -2); // [frames, records]
    if (Type(L, -1) != TYPE_TABLE && create) {
        SetTop(L, -2);
        NewTable(L);
        PushValue(L, 1);
        PushValue(L, -2);
        RawSet(L, -4); // frames[self] = records
    }
    Remove(L, -2); // [records]
    if (Type(L, -1) != TYPE_TABLE)
        return;
    RawGetI(L, -1, record); // [records, extras]
    if (Type(L, -1) != TYPE_TABLE && create) {
        SetTop(L, -2);
        NewTable(L);
        PushNumber(L, record);
        PushValue(L, -2);
        RawSet(L, -4); // records[record] = extras
    }
    Remove(L, -2); // [extras]
}

// The `n` of the extra-argument table at absolute stack slot `extras`.
int ExtraCount(void *L, int extras) {
    using namespace Game::Lua;
    PushString(L, "n");
    RawGet(L, extras);
    const int n = static_cast<int>(ToNumber(L, -1));
    SetTop(L, -2);
    return n;
}

// Stores stack slots kFirstExtra..top as the extra arguments of `record`.
void StoreExtras(void *L, int record, int top) {
    using namespace Game::Lua;
    const int count = top >= kFirstExtra ? top - kFirstExtra + 1 : 0;
    PushExtras(L, record, count > 0);
    const int extras = GetTop(L);
    if (Type(L, extras) == TYPE_TABLE) {
        const int was = ExtraCount(L, extras);
        for (int i = 1; i <= count; ++i) {
            PushNumber(L, i);
            PushValue(L, kFirstExtra - 1 + i);
            RawSet(L, extras);
        }
        for (int i = count + 1; i <= was; ++i) {
            PushNumber(L, i);
            PushNil(L);
            RawSet(L, extras);
        }
        PushString(L, "n");
        PushNumber(L, count);
        RawSet(L, extras);
    }
    SetTop(L, -2);
}

int __fastcall Script_AddMessage(void *L) {
    const int top = Game::Lua::GetTop(L);
    reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_SCROLLINGMESSAGEFRAME_ADD_MESSAGE)(L);
    const char *text = Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : nullptr;
    if (text == nullptr || *text == '\0')
        return 0;
    void *frame = Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_SCROLLINGMESSAGEFRAME_LUA_TYPE_ID);
    if (frame == nullptr)
        return 0;
    StoreExtras(L, Game::Read<int>(frame, Offsets::OFF_SCROLLINGMESSAGEFRAME_MESSAGE_POS), top);
    // Scrolled up, the engine adds the message without a layout.
    DisplayRefreshed::MarkDirty(frame);
    return 0;
}

// AddMessage's colour byte (0x00792A1B..0x00792ACC): the value clamped to
// 0..1 (NaN to 1), then trunc(v × 255 + 0.5).
uint32_t ColorByte(double value) {
    double clamped = 1.0;
    if (value < 0.0)
        clamped = 0.0;
    else if (value < 1.0)
        clamped = static_cast<float>(value);
    return static_cast<uint32_t>(static_cast<int>(clamped * 255.0 + 0.5)) & 0xFFu;
}

// The colour AddMessage builds from stack slots 3..5 — alpha 0xFF, R, G, B —
// or the frame font's when they are not all numbers (0x007929CD..0x00792B45).
uint32_t MessageColor(void *L, void *frame) {
    using namespace Game::Lua;
    if (IsNumber(L, 3) && IsNumber(L, 4) && IsNumber(L, 5))
        return 0xFF000000u | (ColorByte(ToNumber(L, 3)) << 16) | (ColorByte(ToNumber(L, 4)) << 8) |
               ColorByte(ToNumber(L, 5));
    const void *font = Game::Read<const void *>(frame, Offsets::OFF_SCROLLINGMESSAGEFRAME_FONT);
    return font != nullptr ? Game::Read<uint32_t>(font, Offsets::OFF_SCROLLINGMESSAGEFRAME_FONT_COLOR) : 0xFFFFFFFFu;
}

// `ScrollingMessageFrame:BackFillMessage(text, r, g, b, ...)`, retail's: the
// message becomes the OLDEST. Retail's mixin pushes the entry at the back of
// its history buffer and drops it when the buffer is full ("Won't overwrite
// front"); 4.3.4's AddMessage(..., addToTop) writes the same slot but, full,
// overwrites the oldest. The slot is 4.3.4's: once the count has grown, the
// free one before the oldest, `(pos - count + 1) mod max` (pos 0 for an empty
// ring), and the record is written as the ring writer 0x00788150 writes it —
// the text a Storm copy (the ring frees its texts with SMemFree), AddMessage's
// colour, shown, the frame's two times, id 0. Nothing already in the ring
// moves, so a scrolled-up frame keeps its view; at the bottom the lines are
// laid out again from the newest, as the writer does, which shows the new
// line when there is room above the others.
int __fastcall Script_BackFillMessage(void *L) {
    using namespace Game::Lua;
    using StormAlloc_t = void *(__stdcall *)(size_t size, const char *file, int line, int flags);
    using StormFree_t = int(__stdcall *)(void *ptr, const char *file, int line, int flags);
    using Layout_t = void(__fastcall *)(void *smf, void *edx, int newestRecord);
    static const char kStormFile[] = "ClassicAPI ScrollingMessageFrame:BackFillMessage";

    const int top = GetTop(L);
    void *frame = ResolveTypedObject(L, 1, Offsets::VAR_SCROLLINGMESSAGEFRAME_LUA_TYPE_ID);
    if (frame == nullptr)
        return 0;
    const char *text = IsString(L, 2) ? ToString(L, 2) : nullptr;
    if (text == nullptr || *text == '\0')
        return 0;
    const int max = Game::Read<int>(frame, Offsets::OFF_SCROLLINGMESSAGEFRAME_MAX_MESSAGES);
    int &count = Game::Ref<int>(frame, Offsets::OFF_SCROLLINGMESSAGEFRAME_MESSAGE_COUNT);
    if (max <= 0 || count >= max)
        return 0; // full: the message is older than every kept one
    const uint32_t color = MessageColor(L, frame);
    const size_t bytes = std::strlen(text) + 1;
    char *copy = static_cast<char *>(
        reinterpret_cast<StormAlloc_t>(Offsets::FUN_STORM_SMEM_ALLOC)(bytes, kStormFile, __LINE__, 0));
    if (copy == nullptr)
        return 0;
    std::memcpy(copy, text, bytes);

    ++count;
    int &pos = Game::Ref<int>(frame, Offsets::OFF_SCROLLINGMESSAGEFRAME_MESSAGE_POS);
    if (pos == -1)
        pos = 0;
    int slot = pos - count + 1;
    if (slot < 0)
        slot += max;
    uint8_t *record = Game::Read<uint8_t *>(frame, Offsets::OFF_SCROLLINGMESSAGEFRAME_MESSAGES) +
                      slot * Offsets::SCROLLINGMESSAGEFRAME_MESSAGE_STRIDE;
    char *&stored = *reinterpret_cast<char **>(record + Offsets::OFF_SCROLLINGMESSAGEFRAME_MESSAGE_TEXT);
    if (stored != nullptr)
        reinterpret_cast<StormFree_t>(Offsets::FUN_STORM_SMEM_FREE)(stored, kStormFile, __LINE__, 0);
    stored = copy;
    *reinterpret_cast<uint32_t *>(record + Offsets::OFF_SCROLLINGMESSAGEFRAME_MESSAGE_COLOR) = color;
    *reinterpret_cast<int *>(record + Offsets::OFF_SCROLLINGMESSAGEFRAME_MESSAGE_SHOWN) = 1;
    *reinterpret_cast<uint32_t *>(record + Offsets::OFF_SCROLLINGMESSAGEFRAME_MESSAGE_VISIBLE_TIME) =
        Game::Read<uint32_t>(frame, Offsets::OFF_SCROLLINGMESSAGEFRAME_VISIBLE_TIME);
    *reinterpret_cast<uint32_t *>(record + Offsets::OFF_SCROLLINGMESSAGEFRAME_MESSAGE_FADE_TIME) =
        Game::Read<uint32_t>(frame, Offsets::OFF_SCROLLINGMESSAGEFRAME_FADE_TIME);
    *reinterpret_cast<int *>(record + Offsets::OFF_SCROLLINGMESSAGEFRAME_MESSAGE_ID) = 0;
    StoreExtras(L, slot, top);

    if (Game::Read<int>(frame, Offsets::OFF_SCROLLINGMESSAGEFRAME_AT_BOTTOM) != 0)
        reinterpret_cast<Layout_t>(Offsets::FUN_SMF_DISPLAY_REFRESH)(frame, nullptr, pos);
    // Scrolled up, the message lands without a layout.
    DisplayRefreshed::MarkDirty(frame);
    return 0;
}

int __fastcall Script_GetMessageInfo(void *L) {
    using namespace Game::Lua;
    void *frame = ResolveTypedObject(L, 1, Offsets::VAR_SCROLLINGMESSAGEFRAME_LUA_TYPE_ID);
    if (frame == nullptr)
        return 0;
    if (!IsNumber(L, 2)) {
        Error(L, "Usage: %s:GetMessageInfo(index)", ObjectDisplayName(frame));
        return 0;
    }
    const double index = ToNumber(L, 2);
    const int count = Game::Read<int>(frame, Offsets::OFF_SCROLLINGMESSAGEFRAME_MESSAGE_COUNT);
    if (!(index >= 1 && index <= count) || index != static_cast<int>(index))
        return 0;
    int record = Game::Read<int>(frame, Offsets::OFF_SCROLLINGMESSAGEFRAME_MESSAGE_POS) - count +
                 static_cast<int>(index);
    if (record < 0)
        record += Game::Read<int>(frame, Offsets::OFF_SCROLLINGMESSAGEFRAME_MAX_MESSAGES);
    const auto *message = Game::Read<const uint8_t *>(frame, Offsets::OFF_SCROLLINGMESSAGEFRAME_MESSAGES) +
                          record * Offsets::SCROLLINGMESSAGEFRAME_MESSAGE_STRIDE;
    const uint32_t color = Game::Read<uint32_t>(message, Offsets::OFF_SCROLLINGMESSAGEFRAME_MESSAGE_COLOR);

    PushExtras(L, record, false);
    const int extras = GetTop(L);
    const int n = Type(L, extras) == TYPE_TABLE ? ExtraCount(L, extras) : 0;
    CheckStack(L, 4 + n);
    PushString(L, Game::Read<const char *>(message, Offsets::OFF_SCROLLINGMESSAGEFRAME_MESSAGE_TEXT));
    PushNumber(L, ((color >> 16) & 0xFF) / 255.0);
    PushNumber(L, ((color >> 8) & 0xFF) / 255.0);
    PushNumber(L, (color & 0xFF) / 255.0);
    for (int i = 1; i <= n; ++i)
        RawGetI(L, extras, i);
    Remove(L, extras);
    return 4 + n;
}

const Game::Lua::FrameMethodEntry kMethods[] = {
    {"AddMessage", &Script_AddMessage},
    {"BackFillMessage", &Script_BackFillMessage},
    {"GetMessageInfo", &Script_GetMessageInfo},
};

const Game::Doc::Field kAddMessageArgs[] = {
    Game::Doc::Req("text", "string"),
    Game::Doc::Opt("r", "number", nullptr, "Without r, g and b the message takes the frame's font colour."),
    Game::Doc::Opt("g", "number"),
    Game::Doc::Opt("b", "number"),
    Game::Doc::Vararg("any", "Kept with the message; GetMessageInfo returns them after its colour."),
};
const Game::Doc::Function kAddMessage{"Adds a message to the frame.", kAddMessageArgs, {}};

const Game::Doc::Field kBackFillMessageArgs[] = {
    Game::Doc::Req("text", "string"),
    Game::Doc::Opt("r", "number", nullptr, "Without r, g and b the message takes the frame's font colour."),
    Game::Doc::Opt("g", "number"),
    Game::Doc::Opt("b", "number"),
    Game::Doc::Vararg("any", "Kept with the message; GetMessageInfo returns them after its colour."),
};
const Game::Doc::Function kBackFillMessage{
    "Adds a message as the oldest, to fill in history behind the messages the frame has. A frame at "
    "its line limit drops it.",
    kBackFillMessageArgs, {}};

const Game::Doc::Field kGetMessageInfoArgs[] = {
    Game::Doc::Req("index", "luaIndex", "1 is the oldest message."),
};
const Game::Doc::Field kGetMessageInfoRets[] = {
    Game::Doc::Req("text", "string"),
    Game::Doc::Req("r", "number"),
    Game::Doc::Req("g", "number"),
    Game::Doc::Req("b", "number"),
    Game::Doc::Vararg("any", "The arguments AddMessage took after the colour."),
};
const Game::Doc::Function kGetMessageInfo{
    "Returns a message's text, its colour, and the extra arguments it was added with, or nothing "
    "when there is no message at that index.",
    kGetMessageInfoArgs, kGetMessageInfoRets};

const Game::Doc::Method kDocs[] = {
    {"AddMessage", &kAddMessage},
    {"BackFillMessage", &kBackFillMessage},
    {"GetMessageInfo", &kGetMessageInfo},
};

void RegisterLuaFunctions() {
    // Each registration pass is a new Lua state; the old state's ref is gone.
    g_extrasRef = kNoRef;
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_SCROLLINGMESSAGEFRAME_METHOD_REGISTRY),
                                    kMethods, 3, kDocs, 3);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace ScrollingMessageFrame::MessageInfo
