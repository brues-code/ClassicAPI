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

// `region:SetToDefaults()` — puts a frame, texture or font string back in the
// state its class constructs it in (probed on 5.5.4,
// docs/notes/mouse-motion-probe.md): not the state it was created in, so a
// template's values, a font string's creation font and a button's
// construction-time mouse input are all lost. Kept: the parent, keyboard and
// mouse-wheel input, registered events, child regions, and the fields on the
// object's Lua table. Blizzard_Menu's pools reset every frame they hand out
// with it.
//
// Each value is reset through the object's own method, looked up through its
// vtable's method lookup (what the shared metatable's __index calls), so a
// field on the object's table or a replaced metatable — the Compositor
// replaces them — is never consulted, and every ClassicAPI backport keeps its
// own state in step. 1.12's construction values agree with the probed reset:
// strata MEDIUM, the level one above the parent's, a font string with no font
// (+0xE0 = 0), white text, a black shadow at (0, 0), CENTER/MIDDLE and the
// gradient start -1 (CSimpleFontString's constructor, 0x00770D30).
//
// Engine paths, not methods, where the method cannot express the default:
// - Scripts: every slot Frame::ScriptSlots visits is released and emptied, the
//   two writes SetScript(name, nil) makes (0x007748D0).
// - Attributes: Frame::Attributes::ClearAll, firing no OnAttributeChanged.
// - A font string's text, and a button label's, through the font string's
//   SetText member: the Lua SetText refuses a string with no font, and a
//   button's ignores nil. The label itself stays, as on retail.
// - Its font: unlinked from its font object (LinkFontFollower with none), then
//   cleared by SetFont with no path, the engine's own "no font" store.
// - Its follower mask back to the constructor's 0x1F, which each colour,
//   shadow, justify and spacing setter clears a bit of: the next SetFontObject
//   then applies the whole font, as on a new string.
// - Its alpha gradient back to (-1, 0) through its member.
//
// Values 1.12 has no counterpart for are not touched (fixed strata and level,
// ignoring the parent's alpha or scale, flattened render layers, collapsing
// layouts, texture tiling and pixel snapping, text scale). Other frame types
// reset as a Frame.

#include "Game.h"
#include "Offsets.h"
#include "frame/Attributes.h"
#include "frame/ScriptSlots.h"

#include <cstdint>
#include <initializer_list>

namespace Frame::Defaults {

namespace {

using GetScriptMethod_t = int(__fastcall *)(void *self, void *edx, void *L, const char *name);
using Release_t = void(__stdcall *)(uint32_t *slot);
using SetText_t = void(__thiscall *)(void *fs, const char *text, int expand);
using LinkFont_t = void(__thiscall *)(void *holder, void *fontObject);
using SetFont_t = uint32_t(__thiscall *)(void *fs, const char *path, float height, uint32_t flags, uint8_t force);
using SetGradient_t = int(__thiscall *)(void *fs, int start, int length);

constexpr uint32_t kFontFollowAll = 0x1F;

struct Arg {
    enum Kind { NIL, NUMBER, BOOL, STRING } kind;
    double number;
    const char *string;
};
constexpr Arg Num(double n) { return {Arg::NUMBER, n, nullptr}; }
constexpr Arg Bool(bool b) { return {Arg::BOOL, b ? 1.0 : 0.0, nullptr}; }
constexpr Arg Str(const char *s) { return {Arg::STRING, 0.0, s}; }
constexpr Arg Nil() { return {Arg::NIL, 0.0, nullptr}; }

// self:name(args...) on the object at stack index 1, through its own method
// lookup. A method the object's type does not have is skipped.
void Method(void *L, void *obj, const char *name, std::initializer_list<Arg> args = {}) {
    const void *vtable = Game::Read<const void *>(obj, 0);
    if (Game::Read<GetScriptMethod_t>(vtable, Offsets::VTBL_FRAME_GET_SCRIPT_METHOD)(obj, nullptr, L, name) == 0)
        return;
    Game::Lua::PushValue(L, 1);
    for (const Arg &a : args) {
        switch (a.kind) {
        case Arg::NIL:
            Game::Lua::PushNil(L);
            break;
        case Arg::NUMBER:
            Game::Lua::PushNumber(L, a.number);
            break;
        case Arg::BOOL:
            Game::Lua::PushBool(L, a.number != 0.0);
            break;
        case Arg::STRING:
            Game::Lua::PushString(L, a.string);
            break;
        }
    }
    Game::Lua::Call(L, static_cast<int>(args.size()) + 1, 0);
}

void ReleaseSlot(const uint32_t *slot, void * /*ctx*/) {
    auto *s = const_cast<uint32_t *>(slot);
    reinterpret_cast<Release_t>(Offsets::FUN_SCRIPT_RELEASE)(s);
    s[0] = 0;
}

// What every region resets: anchors, size, alpha, shown.
void ResetRegion(void *L, void *obj) {
    Method(L, obj, "ClearAllPoints");
    Method(L, obj, "SetWidth", {Num(0)});
    Method(L, obj, "SetHeight", {Num(0)});
    Method(L, obj, "SetAlpha", {Num(1)});
    Method(L, obj, "Show");
}

void ResetFrame(void *L, void *frame) {
    Frame::ScriptSlots::ForEach(frame, &ReleaseSlot, nullptr);
    Frame::Attributes::ClearAll(L, 1);
    ResetRegion(L, frame);

    const void *parent = Game::Read<const void *>(frame, Offsets::OFF_REGION_PARENT);
    Method(L, frame, "SetScale", {Num(1)});
    Method(L, frame, "SetFrameStrata", {Str("MEDIUM")});
    Method(L, frame, "SetUsingParentLevel", {Bool(false)});
    Method(L, frame, "SetFrameLevel",
           {Num(parent != nullptr ? Game::Read<int>(parent, Offsets::OFF_FRAME_LEVEL) + 1 : 0)});
    Method(L, frame, "EnableMouse", {Bool(false)});
    Method(L, frame, "SetPropagateMouseMotion", {Bool(false)});
    Method(L, frame, "SetPropagateMouseClicks", {Bool(false)});
    Method(L, frame, "SetPropagateKeyboardInput", {Bool(false)});
    Method(L, frame, "SetMovable", {Bool(false)});
    Method(L, frame, "SetResizable", {Bool(false)});
    Method(L, frame, "SetClampedToScreen", {Bool(false)});
    Method(L, frame, "SetClampRectInsets", {Num(0), Num(0), Num(0), Num(0)});
    Method(L, frame, "SetHitRectInsets", {Num(0), Num(0), Num(0), Num(0)});
    Method(L, frame, "SetMinResize", {Num(0), Num(0)});
    Method(L, frame, "SetMaxResize", {Num(0), Num(0)});
    Method(L, frame, "SetToplevel", {Bool(false)});
    Method(L, frame, "SetClipsChildren", {Bool(false)});
    Method(L, frame, "ClearAlphaGradient");
    Method(L, frame, "SetHyperlinksEnabled", {Bool(false)});
    Method(L, frame, "SetDontSavePosition", {Bool(false)});
    Method(L, frame, "SetID", {Num(0)});

    if (!Game::Lua::IsA(frame, Offsets::VAR_BUTTON_LUA_TYPE_ID))
        return;
    if (void *label = Game::Read<void *>(frame, Offsets::OFF_BUTTON_LABEL))
        reinterpret_cast<SetText_t>(Offsets::FUN_FONTSTRING_SET_TEXT)(label, nullptr, 1);
    Method(L, frame, "SetNormalFontObject", {Nil()});
    Method(L, frame, "SetHighlightFontObject", {Nil()});
    Method(L, frame, "SetDisabledFontObject", {Nil()});
    Method(L, frame, "Enable");
    Method(L, frame, "UnlockHighlight");
    Method(L, frame, "SetButtonState", {Str("NORMAL")});
    Method(L, frame, "SetMotionScriptsWhileDisabled", {Bool(false)});
    Method(L, frame, "SetPushedTextOffset", {Num(0), Num(0)});
}

void ResetTexture(void *L, void *tex) {
    ResetRegion(L, tex);
    Method(L, tex, "SetTexture", {Nil()});
    Method(L, tex, "SetTexCoord", {Num(0), Num(1), Num(0), Num(1)});
    Method(L, tex, "SetVertexColor", {Num(1), Num(1), Num(1), Num(1)});
    Method(L, tex, "SetBlendMode", {Str("BLEND")});
    Method(L, tex, "SetDesaturation", {Num(0)});
    Method(L, tex, "SetDrawLayer", {Str("BACKGROUND")});
}

void ResetFontString(void *L, void *fs) {
    reinterpret_cast<SetText_t>(Offsets::FUN_FONTSTRING_SET_TEXT)(fs, nullptr, 1);
    ResetRegion(L, fs);
    reinterpret_cast<LinkFont_t>(Offsets::FUN_FONTSTRING_SET_FONT)(
        Game::Ptr<uint8_t>(fs, Offsets::OFF_FONTSTRING_FONT_HOLDER), nullptr);
    Method(L, fs, "SetTextColor", {Num(1), Num(1), Num(1), Num(1)});
    Method(L, fs, "SetShadowColor", {Num(0), Num(0), Num(0), Num(1)});
    Method(L, fs, "SetShadowOffset", {Num(0), Num(0)});
    Method(L, fs, "SetJustifyH", {Str("CENTER")});
    Method(L, fs, "SetJustifyV", {Str("MIDDLE")});
    Method(L, fs, "SetSpacing", {Num(0)});
    Method(L, fs, "SetWordWrap", {Bool(true)});
    Method(L, fs, "SetNonSpaceWrap", {Bool(false)});
    Method(L, fs, "SetIndentedWordWrap", {Bool(false)});
    Method(L, fs, "SetMaxLines", {Num(0)});
    Method(L, fs, "SetDrawLayer", {Str("BACKGROUND")});
    reinterpret_cast<SetGradient_t>(Offsets::FUN_FONTSTRING_SET_ALPHA_GRADIENT)(fs, -1, 0);
    reinterpret_cast<SetFont_t>(Offsets::FUN_SIMPLEFONTSTRING_SET_FONT)(fs, nullptr, 0.0f, 0, 1);
    Game::Ref<uint32_t>(fs, Offsets::OFF_FONTSTRING_FONT_FLAGS) = kFontFollowAll;
}

int __fastcall Script_SetToDefaults(void *L) {
    void *obj = Game::Lua::ResolveRegion(L);
    if (obj == nullptr)
        return 0;
    Game::Lua::SetTop(L, 1);
    if (Game::Lua::IsA(obj, Offsets::VAR_FRAME_LUA_TYPE_ID))
        ResetFrame(L, obj);
    else if (Game::Lua::IsA(obj, Offsets::VAR_TEXTURE_LUA_TYPE_ID))
        ResetTexture(L, obj);
    else if (Game::Lua::IsA(obj, Offsets::VAR_FONTSTRING_LUA_TYPE_ID))
        ResetFontString(L, obj);
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetToDefaults", &Script_SetToDefaults},
};

const Game::Doc::Function kSetToDefaults{
    "Puts the frame, texture or font string back in the state it has when it is first made, with no template.",
    {}, {}};

const Game::Doc::Method g_methodDocs[] = {
    {"SetToDefaults", &kSetToDefaults},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_REGION_METHOD_REGISTRY), g_methods,
                                    static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
                                    static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::Defaults
