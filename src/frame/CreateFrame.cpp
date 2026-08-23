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
// Modern WoW (2.0+) allows a comma-separated template list in CreateFrame's
// 4th argument:
//
//     CreateFrame("Button", nil, parent,
//                 "UIPanelButtonTemplate, SecureActionButtonTemplate")
//
// Vanilla 1.12's `Script_CreateFrame` (0x007060B0) resolves the 4th arg as a
// SINGLE template name — the whole comma string is one lookup, which fails.
// 1.12 has no native multi-inheritance: the frame builder `FUN_006ee280`
// applies exactly one `inherits` name.
//
// This hook adds real multi-inheritance by mirroring the engine's OWN inherit
// mechanism. It:
//   1. Splits the 4th arg on commas and trims each entry.
//   2. Resolves each name in the XML template registry, keeping the resolvable
//      ones in list order (typos / not-yet-loaded optional deps are skipped).
//   3. Builds the frame through the original `Script_CreateFrame` using the
//      FIRST resolved template — the engine does the object creation, that
//      template's full inherit chain, ref/name setup, and OnLoad.
//   4. Grafts each REMAINING resolved template onto the created frame with the
//      two `__thiscall` calls the builder `FUN_006ee280` emits per inherited
//      node (verified at 0x006ee4ca):
//        content        : frameObj->vtable[+8](templateDefNode, status)        (FUN_00769820)
//        child <Frames> : (frameObj+0x24)->vtable[+8](templateDefNode, status) (FUN_0076a060)
//      Both recurse the grafted template's own `inherits`, add its regions /
//      backdrop / attributes / script handlers, and create its child frames —
//      exactly as if the frame had inherited it. Later templates override
//      earlier ones, matching modern left-to-right inherit order.
//
// OnLoad runs once, during the engine build of the first template (step 3).
// Secondary templates' `<OnLoad>` handlers are registered (they fire on the
// next relevant event) but not re-run at creation, and the frame's OnLoad is
// not re-fired. `status` is the engine's XML-build error accumulator, built
// and reset exactly as `Script_CreateFrame` does.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace Frame::CreateFrame {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);
using TemplateLookup_t = const uint8_t *(__fastcall *)(const char *name);

// XML-node applier vmethod: applies `node` (and its `inherits` chain) to
// `self`. __thiscall(self /*ecx*/, node, status). Reached at vtable byte
// offset +8 on both the frame's main object (content applier, FUN_00769820)
// and its child-<Frames> sub-object at frame+0x24 (FUN_0076a060).
using ApplyNodeFn = void(__thiscall *)(void *self, const void *node, void *status);

// Empties / re-initializes the XML-build status object. __thiscall(this).
using StatusReset_t = void(__thiscall *)(void *self);

// The original Script_CreateFrame function, populated by MinHook.
ScriptFn_t g_origCreateFrame = nullptr;

// ---- helpers ---------------------------------------------------------------

// Returns the definition node if `name` resolves in the engine's XML template
// registry, else nullptr.
const uint8_t *LookupTemplate(const char *name) {
    return reinterpret_cast<TemplateLookup_t>(
        static_cast<uintptr_t>(Offsets::FUN_XML_TEMPLATE_LOOKUP))(name);
}

// Byte offset of the child-<Frames> applier sub-object within a frame object,
// and the vtable slot of the "apply XML node" vmethod. Both read straight from
// the builder FUN_006ee280's apply sequence at 0x006ee4ca:
//   MOV EDX,[EDI]      CALL [EDX+0x20]                    ; top-level content
//   MOV EAX,[EDI+0x24] LEA ECX,[EDI+0x24] CALL [EAX+0x8]  ; child <Frames>
//   MOV EDX,[EDI]      CALL [EDX+0x24]                    ; finalize / OnLoad
// The inherit-recursion inside FUN_00769820 / FUN_0076a060 uses vtable[+8] —
// that is the applier we invoke to graft an extra template.
constexpr int kChildApplierSubObj = 0x24;
constexpr int kApplyNodeVtableSlot = 2;  // byte offset +8 / sizeof(void*)

// The XML-build status object: the 5-dword error accumulator Script_CreateFrame
// stack-builds ({vtable, 8, &self+8, (&self+8)|1, 0}). The appliers only ever
// call its vtable[+0xc] (a __cdecl printf-style logger); building the engine's
// real one absorbs any sub-node warning exactly as the engine does and is safe
// against every slot.
struct BuildStatus {
    const void *vtable;
    int         kind;
    void       *head;
    uintptr_t   tail;
    int         count;
};

void InitStatus(BuildStatus *s) {
    s->vtable = reinterpret_cast<const void *>(
        static_cast<uintptr_t>(Offsets::PTR_TEXLOAD_DESC_VTBL));
    s->kind = 8;
    s->head = &s->head;
    s->tail = reinterpret_cast<uintptr_t>(&s->head) | 1u;
    s->count = 0;
}

void ResetStatus(BuildStatus *s) {
    reinterpret_cast<StatusReset_t>(
        static_cast<uintptr_t>(Offsets::FUN_FRAMESCRIPT_STATUS_RESET))(s);
}

// Grafts template definition `node` onto the already-created frame `frameObj`,
// applying its content and its child <Frames> the same way the engine applies
// an inherited node.
void ApplyTemplateNode(void *frameObj, const void *node, void *status) {
    void **vtbl = *reinterpret_cast<void ***>(frameObj);
    reinterpret_cast<ApplyNodeFn>(vtbl[kApplyNodeVtableSlot])(frameObj, node, status);

    void *childSub = reinterpret_cast<char *>(frameObj) + kChildApplierSubObj;
    void **childVtbl = *reinterpret_cast<void ***>(childSub);
    reinterpret_cast<ApplyNodeFn>(childVtbl[kApplyNodeVtableSlot])(childSub, node, status);
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

        // Note whether we stopped on a comma BEFORE writing the terminator:
        // when there is no trailing whitespace, `end` lands on the comma
        // itself, so `*end = '\0'` would clobber it and the advance below
        // would then miss it — dropping every entry after the first.
        const bool atComma = (*p == ',');

        char *end = p;
        while (end > start && (*(end - 1) == ' ' || *(end - 1) == '\t'))
            --end;
        *end = '\0';

        if (*start != '\0')
            out[count++] = start;

        if (atComma) ++p;
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

    // ---- comma-delimited: split, resolve, combine --------------------------

    char buf[1024];
    const char *names[kMaxTemplates];
    const int count = SplitTemplates(buf, sizeof buf, inherits, names);
    if (count == 0)
        return g_origCreateFrame(L);

    // Resolve each token; keep the resolvable definition nodes in list order.
    const char *resolvedNames[kMaxTemplates];
    const void *resolvedNodes[kMaxTemplates];
    int resolved = 0;
    for (int i = 0; i < count; ++i) {
        const uint8_t *node = LookupTemplate(names[i]);
        if (node != nullptr) {
            resolvedNames[resolved] = names[i];
            resolvedNodes[resolved] = node;
            ++resolved;
        }
    }

    // 0 or 1 resolvable: nothing to combine. Hand the engine a single name —
    // the first resolved one, or (if none resolved) the first token so the
    // engine emits its normal "couldn't find inherited node" error rather than
    // silently building a template-less frame.
    if (resolved <= 1) {
        Game::Lua::SetTop(L, 3);
        Game::Lua::PushString(L, resolved == 1 ? resolvedNames[0] : names[0]);
        return g_origCreateFrame(L);
    }

    // Build with the first resolved template through the engine (object
    // creation + its full inherit chain + ref/name setup + OnLoad).
    Game::Lua::SetTop(L, 3);
    Game::Lua::PushString(L, resolvedNames[0]);
    const int rc = g_origCreateFrame(L);
    if (rc != 1)
        return rc;  // creation failed (e.g. unknown frame type) — leave as-is.

    // Recover the created frame object (top of stack) and graft the remaining
    // templates onto it with the engine's own inherit primitive. Snapshot the
    // stack the engine left (frame at `savedTop`) and restore it afterward, so
    // anything a grafted template's appliers touch on the Lua stack (a child
    // frame's OnLoad, `$parent` name resolution) can't shift the return value
    // the caller reads back — the Tooltip::SetEvents save/restore discipline,
    // which matters in this hook-saturated environment.
    const int savedTop = Game::Lua::GetTop(L);
    void *frameObj = Game::Lua::ResolveObject(L, savedTop);
    if (frameObj != nullptr) {
        BuildStatus status;
        InitStatus(&status);
        for (int i = 1; i < resolved; ++i)
            ApplyTemplateNode(frameObj, resolvedNodes[i], &status);
        ResetStatus(&status);
        Game::Lua::SetTop(L, savedTop);
    }

    return rc;
}

// ---- registration ----------------------------------------------------------

const Game::HookAutoRegister _createFrameHook{
    Offsets::FUN_SCRIPT_CREATEFRAME,
    reinterpret_cast<void *>(&CreateFrame_h),
    reinterpret_cast<void **>(&g_origCreateFrame)};

} // namespace

} // namespace Frame::CreateFrame
