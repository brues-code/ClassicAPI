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

// Multi-template `CreateFrame` support.
//
// Modern WoW (2.0+) allows comma-separated template lists in CreateFrame's 4th
// argument:
//
//     CreateFrame("Button", nil, parent,
//                 "UIPanelButtonTemplate, SecureActionButtonTemplate")
//
// Vanilla 1.12's `Script_CreateFrame` (0x007060B0) only accepts a single
// template name — passing the full comma-separated string causes the lookup
// to fail (no template with that literal compound name exists). The engine's
// own XML `inherits="A, B"` attribute DOES split on commas internally (in the
// XML parser at `FUN_006ede10`), but the Lua-facing `CreateFrame` does not.
//
// This module hooks `Script_CreateFrame` to:
//   1. Detect a comma in the 4th argument.
//   2. Split the comma-delimited list and trim whitespace.
//   3. Pass the first entry to the original `Script_CreateFrame`, creating
//      the frame with that template's full XML inheritance applied.
//
// The first template in the list is typically the "visual" one (e.g.
// `UIPanelButtonTemplate` carrying size, textures, fonts) while subsequent
// entries are behavioral (e.g. `SecureActionButtonTemplate` carrying an
// OnClick handler). The behavioral templates' functionality is provided by
// the C++ `Frame::Attributes` module — which installs a native OnClick
// closure when `SetAttribute("type*", ...)` is called — so not applying the
// second template's XML properties is functionally correct.
//
// If a template name doesn't resolve in the XML registry (typo, optional
// dependency not loaded, etc.) it is skipped; the first resolvable entry wins.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace Frame::CreateFrame {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);
using TemplateLookup_t = const uint8_t *(__fastcall *)(const char *name);

// The original Script_CreateFrame function, populated by MinHook.
ScriptFn_t g_origCreateFrame = nullptr;

// ---- helpers ---------------------------------------------------------------

// Returns non-null if `name` resolves in the engine's XML template registry.
const uint8_t *LookupTemplate(const char *name) {
    return reinterpret_cast<TemplateLookup_t>(
        static_cast<uintptr_t>(Offsets::FUN_XML_TEMPLATE_LOOKUP))(name);
}

// Maximum templates in a single comma-separated list.
constexpr int kMaxTemplates = 8;

// Splits `input` on commas into `out[]`, writing NUL terminators into the
// mutable buffer `buf`. Each entry is whitespace-trimmed. Returns the count.
int SplitTemplates(char *buf, size_t bufLen, const char *input,
                   const char *out[kMaxTemplates]) {
    size_t len = std::strlen(input);
    if (len >= bufLen) len = bufLen - 1;
    std::memcpy(buf, input, len);
    buf[len] = '\0';

    int count = 0;
    char *p = buf;
    while (*p && count < kMaxTemplates) {
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0') break;

        char *start = p;
        while (*p && *p != ',') ++p;

        char *end = p;
        while (end > start && (*(end - 1) == ' ' || *(end - 1) == '\t'))
            --end;
        *end = '\0';

        if (*start != '\0')
            out[count++] = start;

        if (*p == ',') ++p;
    }
    return count;
}

// ---- the hook --------------------------------------------------------------

int __fastcall CreateFrame_h(void *L) {
    // No 4th arg or not a string → pass through.
    if (Game::Lua::GetTop(L) < 4 || !Game::Lua::IsString(L, 4))
        return g_origCreateFrame(L);

    const char *inherits = Game::Lua::ToString(L, 4);
    if (inherits == nullptr || *inherits == '\0')
        return g_origCreateFrame(L);

    // Fast path: no comma → single template, engine handles it natively.
    if (std::strchr(inherits, ',') == nullptr)
        return g_origCreateFrame(L);

    // ---- comma-delimited: split and resolve ---------------------------------

    char buf[1024];
    const char *names[kMaxTemplates];
    const int count = SplitTemplates(buf, sizeof buf, inherits, names);

    if (count == 0)
        return g_origCreateFrame(L);

    // If only one entry after splitting (e.g. trailing comma), use it directly.
    if (count == 1) {
        Game::Lua::SetTop(L, 3);
        Game::Lua::PushString(L, names[0]);
        return g_origCreateFrame(L);
    }

    // Multiple entries: find the first that resolves in the XML template
    // registry. Unresolvable names (typos, optional deps) are skipped.
    const char *resolved = nullptr;
    for (int i = 0; i < count; ++i) {
        if (LookupTemplate(names[i]) != nullptr) {
            resolved = names[i];
            break;
        }
    }

    // Replace arg 4 with the single resolved template (or nil if none found).
    Game::Lua::SetTop(L, 3);
    if (resolved != nullptr)
        Game::Lua::PushString(L, resolved);
    else
        Game::Lua::PushNil(L);

    return g_origCreateFrame(L);
}

// ---- registration ----------------------------------------------------------

const Game::HookAutoRegister _createFrameHook{
    Offsets::FUN_SCRIPT_CREATEFRAME,
    reinterpret_cast<void *>(&CreateFrame_h),
    reinterpret_cast<void **>(&g_origCreateFrame)};

} // namespace

} // namespace Frame::CreateFrame
