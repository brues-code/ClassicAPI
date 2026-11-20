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
// text set from a duration object, as the 3.80.2 client's DurationTextBinding
// does (method table FUN_140c115f0, text FUN_141b96d30, tick FUN_141b969f0):
//
//   - The object is a userdata, tostring "DurationTextBinding: <address>". A
//     new binding is disabled, updates every tick (interval 0) in real time,
//     and has no expired or zero text.
//   - Its text is a format string whose "{}" placeholders each take one
//     component {property, formatter}; SetFormatter(f) is the format "{}"
//     with {RemainingDuration, f}. SetTextFormat raises an error, and keeps
//     the old format, when the counts differ.
//   - The text: with no duration, or a zero one, the zero text (or ""); once
//     the duration has expired, the expired text (or ""); else the format,
//     each value in seconds, the percents 0 to 100.
//   - It writes only with a font string, a duration and a format
//     (CanUpdateFontString), and only a text that differs. Every setter
//     writes at once, also while disabled; enabled, it writes at each tick,
//     or once `interval` seconds of frame time have added up.
//   - The binding keeps the duration object, not a copy: a change to it shows
//     at the next tick. GetDuration returns a new Lua object for it.
//   - Enabling takes no reference: a binding Lua no longer holds stops when
//     it is collected.
//
// Updates run at the head of each UI frame (Tick::FrameTick::AutoSubscribeBefore),
// before its OnUpdate pass and draw, as 3.80.2's CSimpleTop update calls its
// binding tick first (FUN_1405602e0). Destroyed font strings are dropped
// through Frame::RegionDestruction. Assign and Copy are retail's (3.80.2 has
// neither). Not ported: the color curves (SetTextColorCurve,
// GetTextColorCurve, ClearTextColorCurve, GetFormattedTextColor).

#include "Game.h"
#include "lua/Boxed.h"
#include "lua/EnumArg.h"
#include "Offsets.h"
#include "duration/Object.h"
#include "frame/RegionDestruction.h"
#include "text/NumericFormatter.h"
#include "tick/FrameTick.h"

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

constexpr const char *kPlaceholder = "{}";

struct Component {
    int property = Duration::Object::PROPERTY_REMAINING_DURATION;
    Fmt::Formatter *formatter = nullptr; // one reference
};

struct Binding {
    int refs = 0;
    bool enabled = false; // and so in g_active
    bool dead = false;    // freed at the end of the tick that collected it
    void *fontString = nullptr;
    Span *duration = nullptr; // one reference
    std::string textFormat;
    std::vector<Component> components;
    bool hasExpiredText = false;
    std::string expiredText;
    bool hasZeroText = false;
    std::string zeroText;
    int modifier = 0;
    double interval = 0.0;
    double accumulated = 0.0;
};

// Every live binding, for Frame::RegionDestruction; and the enabled ones.
std::vector<Binding *> g_all;
std::vector<Binding *> g_active;
// While a tick runs, bindings collected in it wait here to be freed.
bool g_ticking = false;
std::vector<Binding *> g_dead;

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
    ClearComponents(b);
    b.textFormat.clear();
    b.fontString = nullptr;
    b.hasExpiredText = b.hasZeroText = false;
    b.expiredText.clear();
    b.zeroText.clear();
    b.modifier = 0;
    b.interval = 0.0;
    b.accumulated = 0.0;
}

void Unlink(Binding *b) {
    b->enabled = false;
    const auto it = std::find(g_active.begin(), g_active.end(), b);
    if (it != g_active.end())
        g_active.erase(it);
}

void Free(Binding *b) {
    Clear(*b);
    g_all.erase(std::find(g_all.begin(), g_all.end(), b));
    delete b;
}

void Release(Binding *b) {
    if (b == nullptr || --b->refs != 0)
        return;
    Unlink(b);
    if (g_ticking) {
        b->dead = true;
        g_dead.push_back(b);
    } else {
        Free(b);
    }
}

Binding *New() {
    Binding *b = new Binding;
    g_all.push_back(b);
    return b;
}

bool CanFormat(const Binding &b) { return b.duration != nullptr && !b.textFormat.empty(); }

bool CanUpdate(const Binding &b) { return CanFormat(b) && b.fontString != nullptr; }

bool IsPercent(int property) {
    return property == Duration::Object::PROPERTY_REMAINING_PERCENT ||
           property == Duration::Object::PROPERTY_ELAPSED_PERCENT;
}

double Sample(const Binding &b, int property) {
    const double value = Duration::Object::Sample(b.duration, property, b.modifier);
    return IsPercent(property) ? value * 100.0 : value;
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
    if (b.duration == nullptr || Duration::Object::IsZero(b.duration)) {
        if (b.hasZeroText)
            out = b.zeroText;
        return true;
    }
    if (Duration::Object::HasExpired(b.duration, b.modifier)) {
        if (b.hasExpiredText)
            out = b.expiredText;
        return true;
    }
    size_t next = 0;
    for (size_t i = 0; i < b.textFormat.size(); ++i) {
        if (b.textFormat.compare(i, 2, kPlaceholder) == 0 && next < b.components.size()) {
            const Component &c = b.components[next++];
            if (!AppendFormatted(L, c.formatter, Sample(b, c.property), out))
                return false;
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

// Enable, Disable and SetEnabled each start the interval over.
void SetEnabled(void *L, Binding *b, bool enabled) {
    b->accumulated = 0.0;
    if (!enabled) {
        Unlink(b);
        return;
    }
    if (b->enabled)
        return;
    b->enabled = true;
    g_active.push_back(b);
    Update(L, *b);
}

// The Lua state's bindings go with it.
void PrepareForReload() {
    for (Binding *b : g_active)
        b->enabled = false;
    g_active.clear();
}

const Game::ReloadAutoRegister _reload{&PrepareForReload};

void OnTick() {
    if (g_active.empty())
        return;
    void *L = Lua::State();
    if (L == nullptr)
        return;
    const double elapsed = Tick::FrameTick::Elapsed();
    // A format allocation can run the GC, whose finalizers may collect a
    // binding: it is unlinked at once and freed after the loop.
    g_ticking = true;
    const std::vector<Binding *> active = g_active;
    for (Binding *b : active) {
        if (b->dead || !b->enabled)
            continue;
        if (b->interval > 0.0) {
            b->accumulated += elapsed;
            if (b->accumulated < b->interval)
                continue;
            b->accumulated = 0.0;
        }
        Update(L, *b);
    }
    g_ticking = false;
    for (Binding *b : g_dead)
        Free(b);
    g_dead.clear();
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

Binding *ToBinding(void *L, int idx) {
    return static_cast<Binding *>(Lua::Boxed::To(L, idx, kMetatableKey));
}

void Push(void *L, Binding *b) {
    Retain(b);
    Lua::Boxed::Push(L, kMetatableKey, b);
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
    if (Lua::Type(L, idx) <= Lua::TYPE_NIL) {
        has = false;
        out.clear();
        return true;
    }
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
    std::vector<Component> components = from.components;
    if (duration != nullptr)
        Duration::Object::Retain(duration);
    for (Component &c : components)
        Fmt::Retain(c.formatter);
    Clear(to);
    to.fontString = from.fontString;
    to.duration = duration;
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
    Update(L, *b);
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
    Update(L, *b);
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
    Update(L, *b);
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

// The format "{}" with the remaining duration (FUN_141b96bc0).
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
    ClearComponents(*b);
    b->textFormat = kPlaceholder;
    b->components.push_back(Component{Duration::Object::PROPERTY_REMAINING_DURATION, f});
    Update(L, *b);
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
    int property = -1;
    const bool hasProperty = Lua::Type(L, -1) == Lua::TYPE_NUMBER &&
                             Lua::ToEnumByte(L, -1, Duration::Object::PROPERTY_END_TIME, property);
    Lua::PushString(L, "formatter");
    Lua::RawGet(L, entry);
    c.formatter = Fmt::ToFormatter(L, -1);
    Lua::SetTop(L, entry);
    if (!hasProperty || c.formatter == nullptr)
        return false;
    c.property = property;
    return true;
}

size_t CountPlaceholders(const std::string &format) {
    size_t n = 0;
    for (size_t i = format.find(kPlaceholder); i != std::string::npos; i = format.find(kPlaceholder, i + 2))
        ++n;
    return n;
}

// `components` holds one entry for each "{}" in order. A count that differs
// keeps the old format and raises 3.80.2's error, its numbers in the order it
// passes them (FUN_140c12970).
int __fastcall Script_SetTextFormat(void *L) {
    constexpr const char *kUsage = "self:SetTextFormat(formatString, components)";
    Binding *b = Self(L, kUsage);
    if (b == nullptr)
        return 0;
    if (!Lua::IsString(L, 2)) {
        Usage(L, 2, kUsage);
        return 0;
    }
    if (Lua::Type(L, 3) != Lua::TYPE_TABLE) {
        Usage(L, 3, kUsage);
        return 0;
    }
    std::vector<Component> components;
    bool ok = true;
    for (int i = 1;; ++i) {
        Lua::RawGetI(L, 3, i);
        const int entry = Lua::GetTop(L);
        const bool end = Lua::Type(L, entry) == Lua::TYPE_NIL;
        Component c;
        ok = end || ReadComponent(L, entry, c);
        Lua::SetTop(L, entry - 1);
        if (end || !ok)
            break;
        Fmt::Retain(c.formatter);
        components.push_back(c);
    }
    std::string format(Lua::ToString(L, 2), Lua::StrLen(L, 2));
    const size_t placeholders = CountPlaceholders(format);
    const size_t count = components.size();
    if (!ok || placeholders != count) {
        for (Component &c : components)
            Fmt::Release(c.formatter);
        if (!ok)
            Usage(L, 3, kUsage);
        else
            Lua::Error(L, "DurationTextBinding:SetTextFormat(): expected %u format components for %u placeholders",
                       static_cast<unsigned>(placeholders), static_cast<unsigned>(count));
        return 0;
    }
    ClearComponents(*b);
    b->components = std::move(components);
    b->textFormat = std::move(format);
    Update(L, *b);
    return 0;
}

int __fastcall Script_SetExpiredText(void *L) {
    constexpr const char *kUsage = "self:SetExpiredText([text])";
    if (Binding *b = Self(L, kUsage)) {
        if (OptionalText(L, 2, kUsage, b->hasExpiredText, b->expiredText))
            Update(L, *b);
    }
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
    if (Binding *b = Self(L, kUsage)) {
        if (OptionalText(L, 2, kUsage, b->hasZeroText, b->zeroText))
            Update(L, *b);
    }
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
    Update(L, *b);
    return 0;
}

int __fastcall Script_GetTimeModifier(void *L) {
    Binding *b = Self(L, "local modifier = self:GetTimeModifier()");
    if (b == nullptr)
        return 0;
    Lua::PushNumber(L, b->modifier);
    return 1;
}

// A negative interval is 0 (FUN_141b969f0's setter clamps, with no error).
int __fastcall Script_SetUpdateInterval(void *L) {
    constexpr const char *kUsage = "self:SetUpdateInterval(updateInterval)";
    Binding *b = Self(L, kUsage);
    if (b == nullptr)
        return 0;
    const double n = Lua::Type(L, 2) == Lua::TYPE_NUMBER ? Lua::ToNumber(L, 2) : NAN;
    if (!std::isfinite(n)) {
        Usage(L, 2, kUsage);
        return 0;
    }
    b->interval = n > 0.0 ? n : 0.0;
    b->accumulated = 0.0;
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
        SetEnabled(L, b, false);
        Clear(*b);
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

using Lua::Boxed::Method;
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

// registry[kMetatableKey] = {__index = methods, __gc, __tostring, __metatable = false}
void CreateMetatable(void *L) {
    Lua::Boxed::CreateMetatable(L, kMetatableKey, kMethods, &Meta_gc, &Meta_tostring);
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
