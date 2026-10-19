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

// `Cooldown` frame type, following 4.3.4's CGCooldown
// (wowdecomp15595: WoW/Source/Ui/CGCooldown.{h,cpp},
// CGCooldown_Scripts.cpp).
//
// 4.3.4 has a native CSimpleFrame subclass with its own object type, its
// own script metatable, and five methods:
//
//     SetCooldown(start, duration)   -- seconds, as GetTime() values
//     SetReverse(reverse)            -- StringToBOOL(L, 2, 1)
//     GetReverse()                   -- boolean
//     SetDrawEdge(drawEdge)          -- StringToBOOL(L, 2, 1)
//     GetDrawEdge()                  -- boolean
//
// Vanilla has no such class. Its cooldown sweep is a Model built from
// FrameXML's CooldownFrameTemplate (the UI-Cooldown-Indicator model),
// whose OnUpdateModel script plays sequence 0 as the sweep and sequence 1
// as the end flash, then hides -- the same sweep-then-flash state machine
// CGCooldown::OnLayerUpdate runs in C++. So a Cooldown here is that Model,
// plus this module's five methods with 4.3.4's signatures, argument
// checks, and defaults, driving the model through FrameXML's own
// CooldownFrame_SetTimer -- the exact call 4.3.4's Cooldown.lua makes on
// top of SetCooldown.
//
// "Cooldown" is a real frame type: this module registers it in the
// engine's frame-type table (FUN_FRAMEXML_REGISTER_FACTORY), so XML
// `<Cooldown>` and CreateFrame("Cooldown") both reach CreateCooldown
// through the builder. The factory builds the engine's Model and marks it;
// the frame then loads CooldownFrameTemplate ahead of its own templates
// (BeforeFrameNode), because every 4.3.4 Cooldown draws a sweep.
//
// Two 4.3.4 rendering flags have no counterpart in the indicator model,
// which has neither a reversed sweep nor an edge texture: SetReverse and
// SetDrawEdge store and report their values exactly as 4.3.4 does, but do
// not change how the sweep draws. SetCooldown's stored start/duration
// (milliseconds, as CGCooldown keeps them) are likewise kept for parity
// even though no 4.3.4 getter exposes them.
//
// The methods are registered on the MODEL registry -- the narrowest
// registry a Cooldown resolves through -- so in Lua they are visible on
// every Model. Only a marked Cooldown may call them; any other Model
// gets the engine's own wrong-object-type error, as if the method had
// been called on the wrong class in 4.3.4. GetObjectType / IsObjectType
// and GetFrameType / IsFrameType are re-registered on the same registry
// (most recent registration wins) and delegate to the engine's own
// implementations for every frame that is not a marked Cooldown, so a
// Cooldown reports "Cooldown" while a plain Model is untouched.

#include "Game.h"
#include "Offsets.h"
#include "xml/FrameLoad.h"

#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace Frame::Cooldown {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);

int CallScript(uintptr_t fn, void *L) {
    return reinterpret_cast<ScriptFn_t>(fn)(L);
}

// CGCooldown's per-frame state (CGCooldown.h, +0x1F0..): start/duration
// in milliseconds, the two XML/Lua flags. Elapsed/flashing are the
// model's business here -- its OnUpdateModel script tracks them.
struct State {
    uint32_t startMs = 0;
    uint32_t durationMs = 0;
    bool reverse = false;
    bool drawEdge = false;
    // CooldownFrameTemplate has been loaded under the frame's first node.
    bool templateLoaded = false;
};

// Keyed by the frame object. Frames live until the UI is torn down, and
// a /reload rebuilds every one of them, so the map is cleared on reload.
std::unordered_map<void *, State> g_states;

State *Find(void *frameObject) {
    const auto it = g_states.find(frameObject);
    return it != g_states.end() ? &it->second : nullptr;
}

// True for a frame the Cooldown factory built in this UI session.
bool IsMarked(void *frameObject) {
    return frameObject != nullptr && Find(frameObject) != nullptr;
}

// Resolves `self` as a Model, then requires the Cooldown mark. A Model
// that is not a Cooldown raises the engine's wrong-object-type error,
// the same text an engine method raises for a mismatched `self`.
void *ResolveCooldown(void *L, State **outState) {
    void *obj = Game::Lua::ResolveModel(L);
    if (obj == nullptr)
        return nullptr;
    State *state = Find(obj);
    if (state == nullptr) {
        Game::Lua::Error(
            L, reinterpret_cast<const char *>(Offsets::STR_FS_WRONG_OBJECT_TYPE));
        return nullptr;
    }
    *outState = state;
    return obj;
}

// 4.3.4's StringToBOOL(L, index, 1) (FrameXML/LoadXML.cpp): nil is
// false, a boolean is itself, a number is != 0, a string is read by its
// first letter (0/F/N false, 1-9/T/Y true, plus off/disabled/on/enabled),
// and anything else -- including an unreadable string -- is the default.
bool StringToBool(void *L, int index) {
    switch (Game::Lua::Type(L, index)) {
    case Game::Lua::TYPE_NIL:
        return false;
    case Game::Lua::TYPE_BOOLEAN:
        return Game::Lua::ToBoolean(L, index) != 0;
    case Game::Lua::TYPE_NUMBER:
        return Game::Lua::ToNumber(L, index) != 0.0;
    case Game::Lua::TYPE_STRING: {
        const char *s = Game::Lua::ToString(L, index);
        if (s == nullptr)
            return true;
        switch (*s) {
        case '0': case 'F': case 'N': case 'f': case 'n':
            return false;
        case '1': case '2': case '3': case '4': case '5':
        case '6': case '7': case '8': case '9':
        case 'T': case 'Y': case 't': case 'y':
            return true;
        default:
            break;
        }
        if (_stricmp(s, "off") == 0 || _stricmp(s, "disabled") == 0)
            return false;
        if (_stricmp(s, "on") == 0 || _stricmp(s, "enabled") == 0)
            return true;
        return true;
    }
    default:
        return true;
    }
}

// Drives the sweep through FrameXML's own timer, exactly as stock code
// drives any cooldown Model: CooldownFrame_SetTimer(self, start,
// duration, 1) sets the model's start/duration fields, restarts
// sequence 0 and shows it, or hides it when the timer is not runnable.
// Falls back to a plain Hide if FrameXML has not loaded the function.
void DriveTimer(void *L, double start, double duration) {
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, "CooldownFrame_SetTimer");
    Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_FUNCTION) {
        Game::Lua::SetTop(L, 1); // (self)
        CallScript(Offsets::FUN_SCRIPT_FRAME_HIDE, L);
        Game::Lua::SetTop(L, top);
        return;
    }
    Game::Lua::PushValue(L, 1); // self
    Game::Lua::PushNumber(L, start);
    Game::Lua::PushNumber(L, duration);
    Game::Lua::PushNumber(L, 1); // enable
    Game::Lua::Call(L, 4, 0);
    Game::Lua::SetTop(L, top);
}

// ---- the five Cooldown methods (CGCooldown_Scripts.cpp) -------------------

// OFFSET (4.3.4): 0x009FFBD0 -- errors unless both args are numbers,
// stores start/duration in ms, shows the frame.
int __fastcall Script_SetCooldown(void *L) {
    State *state = nullptr;
    void *obj = ResolveCooldown(L, &state);
    if (obj == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2) || !Game::Lua::IsNumber(L, 3)) {
        Game::Lua::Error(L, "Usage: %s:SetCooldown(start, duration)",
                         Game::Lua::ObjectDisplayName(obj));
        return 0;
    }
    const double start = Game::Lua::ToNumber(L, 2);
    const double duration = Game::Lua::ToNumber(L, 3);
    state->startMs = static_cast<uint32_t>(start * 1000.0);
    state->durationMs = static_cast<uint32_t>(duration * 1000.0);
    DriveTimer(L, start, duration);
    return 0;
}

// OFFSET (4.3.4): 0x009FF650 / 0x009FF6A0
int __fastcall Script_SetReverse(void *L) {
    State *state = nullptr;
    if (ResolveCooldown(L, &state) == nullptr)
        return 0;
    state->reverse = StringToBool(L, 2);
    return 0;
}

int __fastcall Script_GetReverse(void *L) {
    State *state = nullptr;
    if (ResolveCooldown(L, &state) == nullptr)
        return 0;
    Game::Lua::PushBool(L, state->reverse);
    return 1;
}

// OFFSET (4.3.4): 0x009FF6E0 / 0x009FF730
int __fastcall Script_SetDrawEdge(void *L) {
    State *state = nullptr;
    if (ResolveCooldown(L, &state) == nullptr)
        return 0;
    state->drawEdge = StringToBool(L, 2);
    return 0;
}

int __fastcall Script_GetDrawEdge(void *L) {
    State *state = nullptr;
    if (ResolveCooldown(L, &state) == nullptr)
        return 0;
    Game::Lua::PushBool(L, state->drawEdge);
    return 1;
}

// ---- type identity --------------------------------------------------------

// A marked Cooldown answers "Cooldown"; every other Model delegates to
// the engine's own implementation on the untouched stack.
bool IsCooldownArg(void *L) {
    return Game::Lua::IsString(L, 2) &&
           _stricmp(Game::Lua::ToString(L, 2), "Cooldown") == 0;
}

int __fastcall Script_GetObjectType(void *L) {
    void *obj = Game::Lua::ResolveModel(L, 1, /*raiseError=*/false);
    if (obj != nullptr && IsMarked(obj)) {
        Game::Lua::PushString(L, "Cooldown");
        return 1;
    }
    return CallScript(Offsets::FUN_SCRIPT_REGION_GETOBJECTTYPE, L);
}

int __fastcall Script_IsObjectType(void *L) {
    void *obj = Game::Lua::ResolveModel(L, 1, /*raiseError=*/false);
    if (obj != nullptr && IsMarked(obj) && IsCooldownArg(L)) {
        Game::Lua::PushBool(L, true);
        return 1;
    }
    return CallScript(Offsets::FUN_SCRIPT_REGION_ISOBJECTTYPE, L);
}

int __fastcall Script_GetFrameType(void *L) {
    void *obj = Game::Lua::ResolveModel(L, 1, /*raiseError=*/false);
    if (obj != nullptr && IsMarked(obj)) {
        Game::Lua::PushString(L, "Cooldown");
        return 1;
    }
    return CallScript(Offsets::FUN_SCRIPT_FRAME_GETFRAMETYPE, L);
}

int __fastcall Script_IsFrameType(void *L) {
    void *obj = Game::Lua::ResolveModel(L, 1, /*raiseError=*/false);
    if (obj != nullptr && IsMarked(obj) && IsCooldownArg(L)) {
        Game::Lua::PushBool(L, true);
        return 1;
    }
    return CallScript(Offsets::FUN_SCRIPT_FRAME_ISFRAMETYPE, L);
}

// ---- registration ---------------------------------------------------------

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetCooldown", &Script_SetCooldown},
    {"SetReverse", &Script_SetReverse},
    {"GetReverse", &Script_GetReverse},
    {"SetDrawEdge", &Script_SetDrawEdge},
    {"GetDrawEdge", &Script_GetDrawEdge},
    {"GetObjectType", &Script_GetObjectType},
    {"IsObjectType", &Script_IsObjectType},
    {"GetFrameType", &Script_GetFrameType},
    {"IsFrameType", &Script_IsFrameType},
};

const Game::Doc::Field kSetCooldownArgs[] = {
    Game::Doc::Req("start", "number", "Cooldown start time, in seconds as returned by GetTime."),
    Game::Doc::Req("duration", "number", "Cooldown length in seconds."),
};
const Game::Doc::Function kSetCooldown{
    "Starts the cooldown sweep from a start time and duration in seconds.", kSetCooldownArgs, {}};
const Game::Doc::Field kBoolArg[] = {
    Game::Doc::Opt("value", "bool", nullptr, "The flag value; strings and numbers are read as in XML."),
};
const Game::Doc::Field kBoolRet[] = {
    Game::Doc::Req("value", "bool", "The current flag value."),
};
const Game::Doc::Function kSetReverse{
    "Sets whether the cooldown sweep runs in reverse.", kBoolArg, {}};
const Game::Doc::Function kGetReverse{
    "Returns whether the cooldown sweep runs in reverse.", {}, kBoolRet};
const Game::Doc::Function kSetDrawEdge{
    "Sets whether the cooldown draws its bright edge.", kBoolArg, {}};
const Game::Doc::Function kGetDrawEdge{
    "Returns whether the cooldown draws its bright edge.", {}, kBoolRet};

const Game::Doc::Method g_methodDocs[] = {
    {"SetCooldown", &kSetCooldown},
    {"SetReverse", &kSetReverse},
    {"GetReverse", &kGetReverse},
    {"SetDrawEdge", &kSetDrawEdge},
    {"GetDrawEdge", &kGetDrawEdge},
};

// ---- the frame type ---------------------------------------------------------

using Factory_t = void *(__fastcall *)(void *parent);
using RegisterFactory_t = int(__fastcall *)(const char *type, Factory_t factory, int unique);

// 4.3.4's CGCooldown::Create, as vanilla can build it: the engine's own
// Model, marked as a Cooldown.
void *__fastcall CreateCooldown(void *parent) {
    void *frame = reinterpret_cast<Factory_t>(Offsets::FUN_FRAMEXML_CREATE_MODEL)(parent);
    if (frame != nullptr)
        g_states[frame] = State{};
    return frame;
}

// Every 4.3.4 Cooldown draws the sweep; here the sweep is
// CooldownFrameTemplate. So a Cooldown loads it under its first node, as the
// first template it inherits, before the node's own templates and content
// (which can then override it). The template's own load re-enters the frame
// loader with `templateLoaded` already set.
//
// The template is also `setAllPoints`, which pins the frame to its parent; a
// 4.3.4 Cooldown starts unanchored and draws wherever it is put. A Cooldown
// with its own Size + CENTER anchor kept the template's TOPLEFT/BOTTOMRIGHT
// on UIParent too and stretched across the screen (in-game probe: left 0,
// right 2000, three points), so the sweep never showed. A new frame has no
// anchors when its first node loads (the builder's factory and pre-load set
// none), so clearing them after the template restores exactly that. An
// explicit `inherits="CooldownFrameTemplate"` still anchors as vanilla does.
void BeforeFrameNode(void *object, const void * /*node*/, void *status) {
    using ClearAllPoints_t = void(__fastcall *)(void *layout);
    State *state = Find(object);
    if (state == nullptr || state->templateLoaded)
        return;
    state->templateLoaded = true;
    const void *t = Xml::Template("CooldownFrameTemplate");
    if (t == nullptr)
        return;
    Xml::LoadNode(object, t, status);
    reinterpret_cast<ClearAllPoints_t>(Offsets::FUN_LAYOUT_CLEAR_ALL_POINTS)(
        static_cast<uint8_t *>(object) + Offsets::OFF_FRAME_LAYOUT_SUBOBJECT);
}

const Xml::FrameLoad::AutoSubscribeBefore _beforeFrameNode{&BeforeFrameNode};

void RegisterLuaFunctions() {
    // The engine's frame-type table outlives the Lua state, so on a /reload
    // the name is already there and the registrar keeps the first one. This
    // pass runs before FrameXML loads (CGGameUI::Initialize calls
    // LoadScriptFunctions first), like the engine's own game-UI list
    // FUN_00495940, where 4.3.4 registers "Cooldown".
    reinterpret_cast<RegisterFactory_t>(Offsets::FUN_FRAMEXML_REGISTER_FACTORY)(
        "Cooldown", &CreateCooldown, 0);
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_MODEL_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

void PrepareForReload() { g_states.clear(); }

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::ReloadAutoRegister _reload{&PrepareForReload};

} // namespace

} // namespace Frame::Cooldown
