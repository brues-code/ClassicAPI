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

// Template ownership for `Gc::Tag` — 3.3.5's `lua_gctag = taint` around each
// inherited template's application (CSimpleFrame::LoadXML and
// LoadXMLInheritance), so the objects a template's content makes (its compiled
// scripts, mostly) belong to the addon that defined the template, not the one
// instantiating it. A client template registers under context 0, which the
// tag rule reads as "no override".
//
// 1.12 applies a single-name `inherits` at two engine sites
// (CALL_XML_FRAME_LOAD_TEMPLATE_LOOKUP / SITE_XML_FRAME_LOAD_TEMPLATE_APPLY and
// CALL_XML_CHILD_FRAMES_TEMPLATE_LOOKUP / _RECURSE): each looks the name up,
// then applies the node. The lookup calls are redirected to record the
// template's context, the applications are bracketed with the pin. Lists go
// through `Xml::Inherits`, which pins them itself.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "gc/Tag.h"
#include "xml/FrameLoad.h"

#include <cstdint>

namespace Xml::TemplateTag {

namespace {

using ChildFrames_t = void(__fastcall *)(void *frame, void *edx, const void *node, void *status);

// The context of the template the last lookup found; consumed by the
// application that follows it (only a `test eax,eax; jz` lies between).
const char *g_pending = nullptr;

const void *__fastcall LookupTemplate(const char *name) {
    const void *node = Template(name);
    g_pending = node != nullptr ? TemplateContext(name) : nullptr;
    return node;
}

void BeginPin() {
    Gc::Tag::PushPin(g_pending);
    g_pending = nullptr;
}

void EndPin() { Gc::Tag::PopPin(); }

// Stands in for the frame loader's 12-byte re-entry. EAX = the template node,
// EBX = the loader sub-object, EBP = the loader's frame ([ebp+0xC] = status).
__declspec(naked) void ApplyThunk() {
    __asm {
        push eax
        call BeginPin
        pop eax
        mov ecx, [ebp + 0Ch]
        mov edx, [ebx]
        push ecx
        push eax
        mov ecx, ebx
        call dword ptr [edx + 8]
        call EndPin
        ret
    }
}

// Replaces the child-frame builder's recursive call into itself (through the
// entry, so `Xml::Inherits` still sees it).
void __fastcall ChildFramesTemplate(void *frame, void *edx, const void *node, void *status) {
    BeginPin();
    reinterpret_cast<ChildFrames_t>(Offsets::FUN_XML_FRAME_LOAD_CHILD_FRAMES)(frame, edx, node,
                                                                              status);
    EndPin();
}

void PatchCode() {
    static const uint8_t kApply[12] = {0x8B, 0x4D, 0x0C, 0x8B, 0x13, 0x51,
                                       0x50, 0x8B, 0xCB, 0xFF, 0x52, 0x08};
    Common::RedirectCall(Offsets::CALL_XML_FRAME_LOAD_TEMPLATE_LOOKUP,
                         Offsets::FUN_XML_TEMPLATE_LOOKUP,
                         reinterpret_cast<const void *>(&LookupTemplate));
    Common::ReplaceWithCall(Offsets::SITE_XML_FRAME_LOAD_TEMPLATE_APPLY, kApply, sizeof kApply,
                            reinterpret_cast<const void *>(&ApplyThunk));
    Common::RedirectCall(Offsets::CALL_XML_CHILD_FRAMES_TEMPLATE_LOOKUP,
                         Offsets::FUN_XML_TEMPLATE_LOOKUP,
                         reinterpret_cast<const void *>(&LookupTemplate));
    Common::RedirectCall(Offsets::CALL_XML_CHILD_FRAMES_TEMPLATE_RECURSE,
                         Offsets::FUN_XML_FRAME_LOAD_CHILD_FRAMES,
                         reinterpret_cast<const void *>(&ChildFramesTemplate));
}

// Each pass runs before FrameXML and the addons load; the patches are
// idempotent.
const Game::ModuleAutoRegister _autoreg{&PatchCode};

} // namespace

} // namespace Xml::TemplateTag
