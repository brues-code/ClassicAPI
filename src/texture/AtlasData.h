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

#pragma once

#include <cstdint>

// Built-in atlas bindings, derived from `UiTextureAtlas` + `UiTextureAtlasMember`
// as shipped by Classic Era 1.15.9 (build 69547). Vanilla 1.12 has no atlas table
// at all, so a modern addon's `SetAtlas("name")` has nothing to resolve against;
// each row here binds one Blizzard atlas name to the 1.12 texture that carries the
// same art, plus the sub-rect within it.
//
// WHY THIS TABLE IS TINY AND NOT FIVE THOUSAND ROWS
//
// The member table has 5,353 named regions. Almost none of them can be bound
// automatically, for three independent reasons established by measurement:
//
//   1. The sheets do not exist here. Resolving all 868 atlas sheet fileDataIDs
//      through the community listfile and testing every path against the client's
//      own art (7,640 files) gives ZERO hits — Classic Era repacked its art into
//      sheets 1.12 has never shipped.
//   2. Only the names carry over. Blizzard kept the original vanilla filename as
//      the atlas name when packing, so 16 member names match a 1.12 file basename.
//      That is the entire mechanical overlap.
//   3. A matching name does not prove matching art. Comparing the real BLP headers
//      against the member table's sprite sizes, only 5 of those 16 agree.
//      `ui-scrollbar-scrollupbutton-up` is 18x16 in the atlas while the 1.12 file
//      is 32x32 — and vanilla's own `UIPanelScrollBarButton` template draws it at
//      TexCoords 0.25,0.75,0.25,0.75, a CENTERED 16x16 sprite that is neither the
//      atlas's size nor a top-left crop. `macropopup-topleft` is 18x71 against a
//      256x256 file: a different asset that happens to share a name. So texcoords
//      cannot be derived from the two tables, and a wrong guess renders wrong art
//      rather than failing loudly.
//
// HOW A ROW GETS DERIVED
//
// Two methods, both evidence-based. Every row records which one it came from.
//
//   NAME MATCH — the atlas name IS a 1.12 filename AND the dimensions agree, so
//     the whole file is the sprite and the texcoords are a trivial 0..1. Only 5
//     names clear both bars. Do NOT skip the dimension check: 11 more names match
//     while the art does not.
//   WIDGET MATCH — the strong one. Classic Era's UI is this UI rebuilt, so the
//     same widget exists in both trees: one names its art by atlas, the other by
//     file plus TexCoords. Grep `atlas="name"` in Classic Era's exported
//     FrameXML, find the same widget in the client's own exported FrameXML, and
//     read the binding straight off it. Take the SIZE from the 1.12 file rather
//     than the member table — packing adds a pixel or two of bleed.
//
// Anything neither method reaches needs a human to pick the equivalent art by
// eye, which is the lowest-confidence case and always needs a render check.
// `C_Texture.RegisterAtlas` replaces a built-in binding at runtime, so a
// candidate row can be tested live before it is added here.
//
// Growth is driven by the miss log (`_classicapi_DumpAtlasMisses`), never done
// speculatively, so the table only ever moves toward what real addons ask for.
//
// Two source-data traps, handled during extraction:
//   - 28 members have an EMPTY name and must be skipped.
//   - 73 names repeat case-insensitively; keep the higher ID, the same rule
//     `ui/ColorData.h` documents for duplicate color tags.
//
// `tilesHorizontally` / `tilesVertically` come from `CommittedFlags` bits 0 and 1.
// They are stored as resolved booleans rather than the raw flags word because the
// other set bits (4, 8, 16 appear in the data) have no known meaning and must not
// leak into the public contract.

namespace Texture::AtlasData {

struct Entry {
    const char *name;   // Blizzard's canonical casing, returned as `elementName`
    const char *file;   // 1.12 texture path, extension omitted as WoW expects
    int16_t width;      // sprite size, used by SetAtlas's `useAtlasSize`
    int16_t height;
    float left, right, top, bottom; // normalized sub-rect within `file`
    int32_t atlasID;                // Blizzard UiTextureAtlas.ID
    int32_t elementID;              // Blizzard UiTextureAtlasMember.UiTextureAtlasElementID
    bool tilesHorizontally;
    bool tilesVertically;
};

constexpr Entry kAtlases[] = {
    // WIDGET MATCH. Classic Era's FloatingChatFrame.xml draws the chat menu
    // button's `Flash` overlay with atlas "chatframe-button-highlightalert";
    // this client's FloatingChatFrame.xml draws the same `$parentFlash` region
    // with UI-ChatIcon-BlinkHilight and no TexCoords, so the whole file is the
    // sprite. Size is the 1.12 file's 32x32, not the member table's 34x32 (that
    // extra bleed came from packing). Verified in-game: a blue glow ring.
    {"chatframe-button-highlightalert", "Interface\\ChatFrame\\UI-ChatIcon-BlinkHilight",
     32, 32, 0.0f, 1.0f, 0.0f, 1.0f, 924, 10841, false, false},
    // NAME MATCH, dimensions confirmed against the BLP headers.
    {"MinimapArrow", "Interface\\Minimap\\MinimapArrow", 32, 32, 0.0f, 1.0f, 0.0f, 1.0f, 647,
     9441, false, false},
    {"Repair", "Interface\\CURSOR\\Repair", 32, 32, 0.0f, 1.0f, 0.0f, 1.0f, 647, 9509, false,
     false},
    {"Rotating-MinimapArrow", "Interface\\Minimap\\ROTATING-MINIMAPARROW", 32, 32, 0.0f, 1.0f,
     0.0f, 1.0f, 647, 9442, false, false},
    {"Rotating-MinimapGroupArrow", "Interface\\Minimap\\Rotating-MinimapGroupArrow", 32, 32,
     0.0f, 1.0f, 0.0f, 1.0f, 647, 9443, false, false},
    {"Rotating-MinimapGuideArrow", "Interface\\Minimap\\ROTATING-MINIMAPGUIDEARROW", 32, 32,
     0.0f, 1.0f, 0.0f, 1.0f, 647, 9444, false, false},
    // WIDGET MATCH. Classic Era's WowClassicScrollBar (Classic/Scroll/
    // TrimScrollBar.xml) draws its track and steppers with these atlases; this
    // client's scroll bars draw the same art from files. The track is
    // UI-Character-ScrollBar (64x256), the file the Friends and Battlefield
    // frames' scroll bars use: its left column (0..0.484375, 31 px) is the top
    // cap and then rails to the bottom, its right column (0.515625..1) ends in
    // the bottom cap at 0.4140625 (106 px), the TexCoords those frames give
    // it. Each cap is the 28 px at its end, the member table's 31x28. The
    // center tiles vertically there; here it is the whole rail below the top
    // cap, so a stretched center shows the same rails.
    {"UI-ClassicScrollBar-EndCap-Top",
     "Interface\\PaperDollInfoFrame\\UI-Character-ScrollBar", 31, 28, 0.0f,
     0.484375f, 0.0f, 0.109375f, 1941, 18426, false, false},
    {"UI-ClassicScrollBar-EndCap-Bottom",
     "Interface\\PaperDollInfoFrame\\UI-Character-ScrollBar", 31, 28, 0.515625f,
     1.0f, 0.3046875f, 0.4140625f, 1941, 18425, false, false},
    {"UI-ClassicScrollBar-Center",
     "Interface\\PaperDollInfoFrame\\UI-Character-ScrollBar", 31, 228, 0.0f,
     0.484375f, 0.109375f, 1.0f, 1763, 18422, false, true},
    // WIDGET MATCH, the same scroll bar's steppers: this client's
    // UIPanelScrollUp/DownButtonTemplate draws them from these 32x32 files. The
    // arrow in every Up/Down/Disabled file sits at pixels (6,7)..(25,24), alpha
    // measured, which is exactly the member table's 19x17. The Highlight files
    // are full-frame glows the client draws with alphaMode ADD; same rect.
    {"UI-ScrollBar-ClassicScrollUpButton-Up",
     "Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-Up", 19, 17,
     0.1875f, 0.78125f, 0.21875f, 0.75f, 1941, 18434, false, false},
    {"UI-ScrollBar-ClassicScrollUpButton-Down",
     "Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-Down", 19, 17,
     0.1875f, 0.78125f, 0.21875f, 0.75f, 1941, 18432, false, false},
    {"UI-ScrollBar-ClassicScrollUpButton-Disabled",
     "Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-Disabled", 19, 17,
     0.1875f, 0.78125f, 0.21875f, 0.75f, 1941, 18431, false, false},
    {"UI-ScrollBar-ClassicScrollUpButton-Highlight",
     "Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-Highlight", 19, 17,
     0.1875f, 0.78125f, 0.21875f, 0.75f, 1941, 18433, false, false},
    {"UI-ScrollBar-ClassicScrollDownButton-Up",
     "Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-Up", 19, 17,
     0.1875f, 0.78125f, 0.21875f, 0.75f, 1941, 18430, false, false},
    {"UI-ScrollBar-ClassicScrollDownButton-Down",
     "Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-Down", 19, 17,
     0.1875f, 0.78125f, 0.21875f, 0.75f, 1941, 18428, false, false},
    {"UI-ScrollBar-ClassicScrollDownButton-Disabled",
     "Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-Disabled", 19, 17,
     0.1875f, 0.78125f, 0.21875f, 0.75f, 1941, 18427, false, false},
    {"UI-ScrollBar-ClassicScrollDownButton-Highlight",
     "Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-Highlight", 19, 17,
     0.1875f, 0.78125f, 0.21875f, 0.75f, 1941, 18429, false, false},
};

constexpr int kAtlasCount = sizeof(kAtlases) / sizeof(kAtlases[0]);

} // namespace Texture::AtlasData
