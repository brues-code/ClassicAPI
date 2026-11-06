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

// `C_DurationUtil.CreateDurationTextBinding()` and
// `Enum.DurationTextBindingProperty` (DurationTextBindingObjectAPIDocumentation.lua,
// DurationTextBindingSharedDocumentation.lua). A binding keeps a font string's
// text set from a duration object. Retail's behavior, probed in a 12.1.5
// client (2026-11-01; the log is in docs/notes/duration-timer.md):
//
//   - The object is a userdata, tostring "DurationTextBinding: <address>". A
//     new binding is disabled, updates every tick (interval 0), and has no
//     expired or zero text; CanFormatText and CanUpdateFontString are false.
//   - Without a formatter it writes nothing, even enabled: the text stays as
//     it was and GetFormattedText returns "".
//   - With SetFormatter, the text is the formatter's text for the remaining
//     seconds, unrounded (2.949 with "%.1f" -> "2.9"), written at once on
//     enable and then every tick.
//   - The binding keeps the duration object, not a copy: a change to it shows
//     at the next tick. GetDuration returns a new Lua object for it.
//   - Once the duration has expired, the expired text shows; the binding
//     stays enabled.
//
// Not probed, and decided here: a zero (or missing) duration shows the zero
// text when one is set, before the expired text; Assign copies everything
// but the enabled state; Copy starts disabled; a "{}" in SetTextFormat with
// no component writes nothing.
//
// An enabled binding updates with no Lua reference to it (retail's aura
// button keeps one binding because a replaced, still-enabled one would go on
// writing), so enabling takes a reference and disabling drops it. Updates
// run at the head of each UI frame (Tick::FrameTick::AutoSubscribeBefore),
// before its OnUpdate pass and draw, so the text shows in the same frame as a
// timer bar's fill from that pass; at the tail it would show a frame late.
// They set the text only when it differs from the font string's own. Destroyed font strings are
// dropped through Frame::RegionDestruction. Not ported: the color curves
// (SetTextColorCurve, GetTextColorCurve, ClearTextColorCurve,
// GetFormattedTextColor).

#include "Game.h"
#include "Offsets.h"
#include "duration/Object.h"
#include "frame/RegionDestruction.h"
#include "text/NumericFormatter.h"
#include "tick/FrameTick.h"
#include "time/Clock.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Duration::TextBinding {

namespace {

namespace Lua = Game::Lua;
namespace Fmt = Text::NumericFormatter;
using Duration::Object::Span;

struct Component {
    int property = Duration::Object::PROPERTY_REMAINING_DURATION;
    Fmt::Formatter *formatter = nullptr; // one reference
};

struct Binding {
    int refs = 0;
    bool enabled = false;           // and so held by g_active
    void *fontString = nullptr;
    Span *duration = nullptr;       // one reference
    Fmt::Formatter *formatter = nullptr; // SetFormatter; one reference
    bool hasTextFormat = false;     // SetTextFormat
    std::string textFormat;
    std::vector<Component> components;
    bool hasExpiredText = false;
    std::string expiredText;
    bool hasZeroText = false;
    std::string zeroText;
    int modifier = 0;
    double interval = 0.0;
    double lastUpdate = -INFINITY;
};

// Every live binding, for Frame::RegionDestruction; and the enabled ones,
// each holding a reference.
std::vector<Binding *> g_all;
std::vector<Binding *> g_active;

void Retain(Binding *b) { ++b->refs; }

void ClearComponents(Binding &b) {
    for (Component &c : b.components)
        Fmt::Release(c.formatter);
    b.components.clear();
}

// Drops every reference the binding holds and its settings.
void Clear(Binding &b) {
    Duration::Object::Release(b.duration);
    b.duration = nullptr;
    Fmt::Release(b.formatter);
    b.formatter = nullptr;
    ClearComponents(b);
    b.hasTextFormat = false;
    b.textFormat.clear();
    b.fontString = nullptr;
    b.hasExpiredText = b.hasZeroText = false;
    b.expiredText.clear();
    b.zeroText.clear();
    b.modifier = 0;
    b.interval = 0.0;
    b.lastUpdate = -INFINITY;
}

void Release(Binding *b) {
    if (b == nullptr || --b->refs != 0)
        return;
    Clear(*b);
    g_all.erase(std::find(g_all.begin(), g_all.end(), b));
    delete b;
}

Binding *New() {
    Binding *b = new Binding;
    g_all.push_back(b);
    return b;
}

bool CanFormat(const Binding &b) { return b.formatter != nullptr || b.hasTextFormat; }

bool CanUpdate(const Binding &b) { return CanFormat(b) && b.fontString != nullptr; }

double Sample(const Binding &b, int property) {
    return b.duration != nullptr ? Duration::Object::Sample(b.duration, property, b.modifier) : 0.0;
}

// Appends the formatter's text for `value`; false on a format error, whose
// message is then at the top of the stack.
bool AppendFormatted(void *L, Fmt::Formatter *f, double value, std::string &out) {
    const char *text = Fmt::Format(L, f, value);
    if (text == nullptr)
        return false;
    out += text;
    Lua::SetTop(L, -2);
    return true;
}

// The binding's text. False on a format error, whose message is then pushed.
bool Formatted(void *L, const Binding &b, std::string &out) {
    out.clear();
    if (!CanFormat(b))
        return true;
    const bool zero = b.duration == nullptr || Duration::Object::IsZero(b.duration);
    if (zero && b.hasZeroText) {
        out = b.zeroText;
        return true;
    }
    if (b.hasExpiredText && (b.duration == nullptr || Duration::Object::HasExpired(b.duration, b.modifier))) {
        out = b.expiredText;
        return true;
    }
    if (b.formatter != nullptr)
        return AppendFormatted(L, b.formatter, Sample(b, Duration::Object::PROPERTY_REMAINING_DURATION), out);
    size_t next = 0;
    for (size_t i = 0; i < b.textFormat.size(); ++i) {
        if (b.textFormat[i] == '{' && i + 1 < b.textFormat.size() && b.textFormat[i + 1] == '}') {
            if (next < b.components.size()) {
                const Component &c = b.components[next];
                if (!AppendFormatted(L, c.formatter, Sample(b, c.property), out))
                    return false;
            }
            ++next;
            ++i;
        } else {
            out += b.textFormat[i];
        }
    }
    return true;
}

// Writes the binding's text into its font string when it differs. A format
// error writes nothing.
void Update(void *L, Binding &b) {
    if (!CanUpdate(b))
        return;
    b.lastUpdate = Time::Clock::NowSeconds();
    const int top = Lua::GetTop(L);
    std::string text;
    const bool ok = Formatted(L, b, text);
    Lua::SetTop(L, top);
    if (!ok)
        return;
    const char *current = Game::Read<const char *>(b.fontString, Offsets::OFF_FONTSTRING_TEXT);
    if (current != nullptr && text == current)
        return;
    using SetText_t = void(__fastcall *)(void *fs, void *edx, const char *text, int expand);
    reinterpret_cast<SetText_t>(Offsets::FUN_FONTSTRING_SET_TEXT)(b.fontString, nullptr, text.c_str(), 1);
}

void Disable(Binding *b) {
    if (!b->enabled)
        return;
    b->enabled = false;
    g_active.erase(std::find(g_active.begin(), g_active.end(), b));
    Release(b);
}

void SetEnabled(void *L, Binding *b, bool enabled) {
    if (!enabled) {
        Disable(b);
        return;
    }
    if (b->enabled)
        return;
    b->enabled = true;
    Retain(b);
    g_active.push_back(b);
    Update(L, *b);
}

// An enabled binding's own reference outlives the Lua state; drop it before
// the state goes. Copy first: each Disable edits g_active.
void PrepareForReload() {
    const std::vector<Binding *> active = g_active;
    for (Binding *b : active)
        Disable(b);
}

const Game::ReloadAutoRegister _reload{&PrepareForReload};

void OnTick() {
    if (g_active.empty())
        return;
    void *L = Lua::State();
    if (L == nullptr)
        return;
    const double now = Time::Clock::NowSeconds();
    // By index: a format allocation can run the GC, whose finalizers may
    // release (never free) an active binding.
    for (size_t i = 0; i < g_active.size(); ++i) {
        Binding &b = *g_active[i];
        if (b.interval > 0.0 && now - b.lastUpdate < b.interval)
            continue;
        Update(L, b);
    }
}

const Tick::FrameTick::AutoSubscribeBefore _tickSub{&OnTick};

void OnRegionDestroyed(void *region) {
    for (Binding *b : g_all)
        if (b->fontString == region)
            b->fontString = nullptr;
}

const Frame::RegionDestruction::AutoSubscribe _destructionSub{&OnRegionDestroyed};

// ---- the Lua object -------------------------------------------------------------

constexpr const char *kMetatableKey = "ClassicAPI.DurationTextBinding";

void PushMetatable(void *L) {
    Lua::PushString(L, kMetatableKey);
    Lua::RawGet(L, Lua::REGISTRY_INDEX);
}

int AbsIndex(void *L, int idx) { return idx > 0 ? idx : Lua::GetTop(L) + idx + 1; }

Binding *ToBinding(void *L, int idx) {
    if (Lua::Type(L, idx) != Lua::TYPE_USERDATA)
        return nullptr;
    idx = AbsIndex(L, idx);
    if (!Lua::GetMetatable(L, idx))
        return nullptr;
    PushMetatable(L);
    const bool ours = Lua::ToPointer(L, -1) == Lua::ToPointer(L, -2);
    Lua::SetTop(L, -3);
    return ours ? *static_cast<Binding **>(Lua::ToUserdata(L, idx)) : nullptr;
}

void Push(void *L, Binding *b) {
    auto **block = static_cast<Binding **>(Lua::NewUserdata(L, sizeof(Binding *)));
    *block = b;
    Retain(b);
    PushMetatable(L);
    Lua::SetMetatable(L, -2);
}

void Usage(void *L, int arg, const char *usage) { Lua::Error(L, "bad argument #%d to '?' (Usage: %s)", arg, usage); }

Binding *Self(void *L, const char *usage) {
    Binding *b = ToBinding(L, 1);
    if (b == nullptr)
        Usage(L, 1, usage);
    return b;
}

// An optional string at `idx`: `has` false for nil.
bool OptionalText(void *L, int idx, const char *usage, bool &has, std::string &out) {
    has = false;
    out.clear();
    if (Lua::Type(L, idx) <= Lua::TYPE_NIL)
        return true;
    if (!Lua::IsString(L, idx)) {
        Usage(L, idx, usage);
        return false;
    }
    has = true;
    out.assign(Lua::ToString(L, idx), Lua::StrLen(L, idx));
    return true;
}

void PushOptionalText(void *L, bool has, const std::string &text) {
    if (has)
        Lua::PushLString(L, text.data(), static_cast<unsigned int>(text.size()));
    else
        Lua::PushNil(L);
}

// Copies `from`'s settings into `to`, which keeps its enabled state.
void CopySettings(Binding &to, const Binding &from) {
    if (&to == &from)
        return;
    Span *duration = from.duration;
    Fmt::Formatter *formatter = from.formatter;
    std::vector<Component> components = from.components;
    if (duration != nullptr)
        Duration::Object::Retain(duration);
    if (formatter != nullptr)
        Fmt::Retain(formatter);
    for (Component &c : components)
        Fmt::Retain(c.formatter);
    Clear(to);
    to.fontString = from.fontString;
    to.duration = duration;
    to.formatter = formatter;
    to.hasTextFormat = from.hasTextFormat;
    to.textFormat = from.textFormat;
    to.components = std::move(components);
    to.hasExpiredText = from.hasExpiredText;
    to.expiredText = from.expiredText;
    to.hasZeroText = from.hasZeroText;
    to.zeroText = from.zeroText;
    to.modifier = from.modifier;
    to.interval = from.interval;
}

// ---- the methods ----------------------------------------------------------------

int __fastcall Script_Assign(void *L) {
    constexpr const char *kUsage = "self:Assign(other)";
    Binding *b = Self(L, kUsage);
    if (b == nullptr)
        return 0;
    const Binding *other = ToBinding(L, 2);
    if (other == nullptr) {
        Usage(L, 2, kUsage);
        return 0;
    }
    CopySettings(*b, *other);
    return 0;
}

int __fastcall Script_Copy(void *L) {
    Binding *b = Self(L, "local copy = self:Copy()");
    if (b == nullptr)
        return 0;
    Binding *copy = New();
    CopySettings(*copy, *b);
    Push(L, copy);
    return 1;
}

int __fastcall Script_CanFormatText(void *L) {
    Binding *b = Self(L, "local canFormatText = self:CanFormatText()");
    if (b == nullptr)
        return 0;
    Lua::PushBool(L, CanFormat(*b));
    return 1;
}

int __fastcall Script_CanUpdateFontString(void *L) {
    Binding *b = Self(L, "local canUpdateText = self:CanUpdateFontString()");
    if (b == nullptr)
        return 0;
    Lua::PushBool(L, CanUpdate(*b));
    return 1;
}

int __fastcall Script_Enable(void *L) {
    if (Binding *b = Self(L, "self:Enable()"))
        SetEnabled(L, b, true);
    return 0;
}

int __fastcall Script_Disable(void *L) {
    if (Binding *b = Self(L, "self:Disable()"))
        SetEnabled(L, b, false);
    return 0;
}

int __fastcall Script_SetEnabled(void *L) {
    if (Binding *b = Self(L, "self:SetEnabled(enabled)"))
        SetEnabled(L, b, Lua::ToFlag(L, 2));
    return 0;
}

int __fastcall Script_IsEnabled(void *L) {
    Binding *b = Self(L, "local enabled = self:IsEnabled()");
    if (b == nullptr)
        return 0;
    Lua::PushBool(L, b->enabled);
    return 1;
}

int __fastcall Script_SetDuration(void *L) {
    constexpr const char *kUsage = "self:SetDuration(duration)";
    Binding *b = Self(L, kUsage);
    if (b == nullptr)
        return 0;
    Span *duration = Duration::Object::ToSpan(L, 2);
    if (duration == nullptr) {
        Usage(L, 2, kUsage);
        return 0;
    }
    Duration::Object::Retain(duration);
    Duration::Object::Release(b->duration);
    b->duration = duration;
    return 0;
}

int __fastcall Script_GetDuration(void *L) {
    Binding *b = Self(L, "local duration = self:GetDuration()");
    if (b == nullptr)
        return 0;
    if (b->duration != nullptr)
        Duration::Object::Push(L, b->duration);
    else
        Lua::PushNil(L);
    return 1;
}

int __fastcall Script_SetFontString(void *L) {
    constexpr const char *kUsage = "self:SetFontString(fontString)";
    Binding *b = Self(L, kUsage);
    if (b == nullptr)
        return 0;
    void *fontString = Lua::ResolveFontString(L, 2, false);
    if (fontString == nullptr) {
        Usage(L, 2, kUsage);
        return 0;
    }
    b->fontString = fontString;
    return 0;
}

int __fastcall Script_GetFontString(void *L) {
    Binding *b = Self(L, "local fontString = self:GetFontString()");
    if (b == nullptr)
        return 0;
    if (b->fontString != nullptr)
        Lua::PushObject(L, b->fontString);
    else
        Lua::PushNil(L);
    return 1;
}

int __fastcall Script_SetFormatter(void *L) {
    constexpr const char *kUsage = "self:SetFormatter(formatter)";
    Binding *b = Self(L, kUsage);
    if (b == nullptr)
        return 0;
    Fmt::Formatter *f = Fmt::ToFormatter(L, 2);
    if (f == nullptr) {
        Usage(L, 2, kUsage);
        return 0;
    }
    Fmt::Retain(f);
    Fmt::Release(b->formatter);
    b->formatter = f;
    ClearComponents(*b);
    b->hasTextFormat = false;
    b->textFormat.clear();
    return 0;
}

// The component at stack index `entry`: {property =
// Enum.DurationTextBindingProperty, formatter = NumericFormatter}. Leaves the
// stack as it was; false when the entry is not one.
bool ReadComponent(void *L, int entry, Component &c) {
    if (Lua::Type(L, entry) != Lua::TYPE_TABLE)
        return false;
    Lua::PushString(L, "property");
    Lua::RawGet(L, entry);
    const double p = Lua::Type(L, -1) == Lua::TYPE_NUMBER ? Lua::ToNumber(L, -1) : NAN;
    Lua::PushString(L, "formatter");
    Lua::RawGet(L, entry);
    c.formatter = Fmt::ToFormatter(L, -1);
    Lua::SetTop(L, entry);
    if (!(p >= Duration::Object::PROPERTY_REMAINING_DURATION && p <= Duration::Object::PROPERTY_END_TIME) ||
        p != static_cast<int>(p) || c.formatter == nullptr)
        return false;
    c.property = static_cast<int>(p);
    return true;
}

// Replaces the binding's text format with `formatString` and the component
// list at stack index `t`, all or nothing.
bool ApplyTextFormat(void *L, Binding &b, int formatString, int t) {
    std::vector<Component> components;
    for (int i = 1;; ++i) {
        Lua::RawGetI(L, t, i);
        const int entry = Lua::GetTop(L);
        Component c;
        const bool end = Lua::Type(L, entry) == Lua::TYPE_NIL;
        const bool ok = !end && ReadComponent(L, entry, c);
        Lua::SetTop(L, entry - 1);
        if (end)
            break;
        if (!ok) {
            for (Component &read : components)
                Fmt::Release(read.formatter);
            return false;
        }
        Fmt::Retain(c.formatter);
        components.push_back(c);
    }
    Fmt::Release(b.formatter);
    b.formatter = nullptr;
    ClearComponents(b);
    b.components = std::move(components);
    b.textFormat.assign(Lua::ToString(L, formatString), Lua::StrLen(L, formatString));
    b.hasTextFormat = true;
    return true;
}

// `components` holds one entry for each "{}" in order.
int __fastcall Script_SetTextFormat(void *L) {
    constexpr const char *kUsage = "self:SetTextFormat(formatString, components)";
    Binding *b = Self(L, kUsage);
    if (b == nullptr)
        return 0;
    if (!Lua::IsString(L, 2)) {
        Usage(L, 2, kUsage);
        return 0;
    }
    if (Lua::Type(L, 3) != Lua::TYPE_TABLE || !ApplyTextFormat(L, *b, 2, 3))
        Usage(L, 3, kUsage);
    return 0;
}

int __fastcall Script_SetExpiredText(void *L) {
    constexpr const char *kUsage = "self:SetExpiredText([text])";
    if (Binding *b = Self(L, kUsage))
        OptionalText(L, 2, kUsage, b->hasExpiredText, b->expiredText);
    return 0;
}

int __fastcall Script_GetExpiredText(void *L) {
    Binding *b = Self(L, "local text = self:GetExpiredText()");
    if (b == nullptr)
        return 0;
    PushOptionalText(L, b->hasExpiredText, b->expiredText);
    return 1;
}

int __fastcall Script_SetZeroDurationText(void *L) {
    constexpr const char *kUsage = "self:SetZeroDurationText([text])";
    if (Binding *b = Self(L, kUsage))
        OptionalText(L, 2, kUsage, b->hasZeroText, b->zeroText);
    return 0;
}

int __fastcall Script_GetZeroDurationText(void *L) {
    Binding *b = Self(L, "local text = self:GetZeroDurationText()");
    if (b == nullptr)
        return 0;
    PushOptionalText(L, b->hasZeroText, b->zeroText);
    return 1;
}

int __fastcall Script_SetTimeModifier(void *L) {
    constexpr const char *kUsage = "self:SetTimeModifier(modifier)";
    Binding *b = Self(L, kUsage);
    if (b == nullptr)
        return 0;
    int m;
    if (Lua::Type(L, 2) <= Lua::TYPE_NIL || !Duration::Object::ToModifier(L, 2, m)) {
        Usage(L, 2, kUsage);
        return 0;
    }
    b->modifier = m;
    return 0;
}

int __fastcall Script_GetTimeModifier(void *L) {
    Binding *b = Self(L, "local modifier = self:GetTimeModifier()");
    if (b == nullptr)
        return 0;
    Lua::PushNumber(L, b->modifier);
    return 1;
}

int __fastcall Script_SetUpdateInterval(void *L) {
    constexpr const char *kUsage = "self:SetUpdateInterval(updateInterval)";
    Binding *b = Self(L, kUsage);
    if (b == nullptr)
        return 0;
    const double n = Lua::Type(L, 2) == Lua::TYPE_NUMBER ? Lua::ToNumber(L, 2) : NAN;
    if (!(n >= 0.0) || !std::isfinite(n)) {
        Usage(L, 2, kUsage);
        return 0;
    }
    b->interval = n;
    return 0;
}

int __fastcall Script_GetUpdateInterval(void *L) {
    Binding *b = Self(L, "local updateInterval = self:GetUpdateInterval()");
    if (b == nullptr)
        return 0;
    Lua::PushNumber(L, b->interval);
    return 1;
}

int __fastcall Script_GetFormattedText(void *L) {
    Binding *b = Self(L, "local text = self:GetFormattedText()");
    if (b == nullptr)
        return 0;
    bool ok;
    { // the string is gone before lua_error, which unwinds no C++ frame
        std::string text;
        ok = Formatted(L, *b, text);
        if (ok)
            Lua::PushLString(L, text.data(), static_cast<unsigned int>(text.size()));
    }
    if (!ok) {
        const char *msg = Lua::ToString(L, -1); // the format error, on the stack
        Lua::Error(L, "DurationTextBinding:GetFormattedText(): %s", msg != nullptr ? msg : "?");
        return 0;
    }
    return 1;
}

int __fastcall Script_UpdateFontString(void *L) {
    if (Binding *b = Self(L, "self:UpdateFontString()"))
        Update(L, *b);
    return 0;
}

// Clears everything and disables the binding.
int __fastcall Script_SetToDefaults(void *L) {
    if (Binding *b = Self(L, "self:SetToDefaults()")) {
        Retain(b); // SetEnabled(false) may drop the last reference but ours
        SetEnabled(L, b, false);
        Clear(*b);
        Release(b);
    }
    return 0;
}

int __fastcall Script_HasSecretValues(void *L) {
    if (Self(L, "local hasSecretValues = self:HasSecretValues()") == nullptr)
        return 0;
    Lua::PushBool(L, false);
    return 1;
}

int __fastcall Meta_gc(void *L) {
    auto **block = static_cast<Binding **>(Lua::ToUserdata(L, 1));
    if (block != nullptr) {
        Release(*block);
        *block = nullptr;
    }
    return 0;
}

int __fastcall Meta_tostring(void *L) {
    char text[48];
    std::snprintf(text, sizeof(text), "DurationTextBinding: %p", static_cast<void *>(ToBinding(L, 1)));
    Lua::PushString(L, text);
    return 1;
}

int __fastcall Script_CreateDurationTextBinding(void *L) {
    Push(L, New());
    return 1;
}

struct Method {
    const char *name;
    Lua::CFunction fn;
};
const Method kMethods[] = {
    {"Assign", &Script_Assign},
    {"CanFormatText", &Script_CanFormatText},
    {"CanUpdateFontString", &Script_CanUpdateFontString},
    {"Copy", &Script_Copy},
    {"Disable", &Script_Disable},
    {"Enable", &Script_Enable},
    {"GetDuration", &Script_GetDuration},
    {"GetExpiredText", &Script_GetExpiredText},
    {"GetFontString", &Script_GetFontString},
    {"GetFormattedText", &Script_GetFormattedText},
    {"GetTimeModifier", &Script_GetTimeModifier},
    {"GetUpdateInterval", &Script_GetUpdateInterval},
    {"GetZeroDurationText", &Script_GetZeroDurationText},
    {"HasSecretValues", &Script_HasSecretValues},
    {"IsEnabled", &Script_IsEnabled},
    {"SetDuration", &Script_SetDuration},
    {"SetEnabled", &Script_SetEnabled},
    {"SetExpiredText", &Script_SetExpiredText},
    {"SetFontString", &Script_SetFontString},
    {"SetFormatter", &Script_SetFormatter},
    {"SetTextFormat", &Script_SetTextFormat},
    {"SetTimeModifier", &Script_SetTimeModifier},
    {"SetToDefaults", &Script_SetToDefaults},
    {"SetUpdateInterval", &Script_SetUpdateInterval},
    {"SetZeroDurationText", &Script_SetZeroDurationText},
    {"UpdateFontString", &Script_UpdateFontString},
};

void SetFunction(void *L, const char *key, Lua::CFunction fn) {
    Lua::PushString(L, key);
    Lua::PushCClosure(L, fn, 0);
    Lua::SetTable(L, -3);
}

// registry[kMetatableKey] = {__index = methods, __gc, __tostring, __metatable = false}
void CreateMetatable(void *L) {
    PushMetatable(L);
    const bool exists = Lua::Type(L, -1) == Lua::TYPE_TABLE;
    Lua::SetTop(L, -2);
    if (exists)
        return;
    Lua::PushString(L, kMetatableKey);
    Lua::NewTable(L);
    Lua::PushString(L, "__index");
    Lua::NewTable(L);
    for (const Method &m : kMethods)
        SetFunction(L, m.name, m.fn);
    Lua::SetTable(L, -3);
    SetFunction(L, "__gc", &Meta_gc);
    SetFunction(L, "__tostring", &Meta_tostring);
    Lua::PushString(L, "__metatable");
    Lua::PushBool(L, false);
    Lua::SetTable(L, -3);
    Lua::RawSet(L, Lua::REGISTRY_INDEX);
}

const Lua::EnumIntegerEntry kProperties[] = {
    {"RemainingDuration", Duration::Object::PROPERTY_REMAINING_DURATION},
    {"RemainingPercent", Duration::Object::PROPERTY_REMAINING_PERCENT},
    {"ElapsedDuration", Duration::Object::PROPERTY_ELAPSED_DURATION},
    {"ElapsedPercent", Duration::Object::PROPERTY_ELAPSED_PERCENT},
    {"TotalDuration", Duration::Object::PROPERTY_TOTAL_DURATION},
    {"StartTime", Duration::Object::PROPERTY_START_TIME},
    {"EndTime", Duration::Object::PROPERTY_END_TIME},
};

const Game::Doc::Field kCreateRets[] = {
    Game::Doc::Req("binding", "DurationTextBinding"),
};
const Game::Doc::Function kCreateDurationTextBinding{
    "Creates a binding that keeps a font string's text set from a duration object.", {}, kCreateRets};

void RegisterLuaFunctions() {
    CreateMetatable(Lua::State());
    Lua::RegisterTableFunction("C_DurationUtil", "CreateDurationTextBinding", &Script_CreateDurationTextBinding,
                               &kCreateDurationTextBinding);
    Lua::RegisterIntegerEnum(
        "Enum", "DurationTextBindingProperty", kProperties, "DurationTextBindingShared");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Duration::TextBinding
