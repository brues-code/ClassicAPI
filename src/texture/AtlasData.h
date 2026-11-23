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
// `tilesHorizontally` / `tilesVertically` come from `CommittedFlags` 4 and 2:
// 190 of the 194 members named `_...` (Blizzard's prefix for art that tiles
// across) carry 4, and 56 of the 60 named `!...` (tiles down) carry 2; 1 is
// set only on the nameless rows. They are stored as resolved booleans rather
// than the raw flags word because the other set bits (8, 16 appear in the data)
// have no known meaning and must not leak into the public contract.

namespace Texture::AtlasData {

struct Slice {
    int16_t left, top, right, bottom;
    uint8_t mode;
};

struct Entry {
    const char *name;  // Blizzard's canonical casing, returned as `elementName`
    const char *file;   // 1.12 texture path, extension omitted as WoW expects
    int16_t width;      // sprite size, used by SetAtlas's `useAtlasSize`
    int16_t height;
    float left, right, top, bottom; // normalized sub-rect within `file`
    int32_t atlasID;                // Blizzard UiTextureAtlas.ID
    int32_t elementID;              // Blizzard UiTextureAtlasMember.UiTextureAtlasElementID
    bool tilesHorizontally;
    bool tilesVertically;
    // The sprite is the rect transposed: sprite x runs down the file rect and
    // sprite y across it, so the rect's top edge is the sprite's left edge and
    // `width` / `height` are the rect's height / width. This client has no
    // horizontal scroll bar art; transposing the vertical art keeps its
    // top-left lighting, which a quarter turn would move.
    bool transposed;
    // The nine-slice data SetAtlas applies (Texture::Slice): margins in the
    // sprite's pixels and the mode (Enum.UITextureSliceMode, 0 stretched,
    // 1 tiled). Retail keeps it in UiTextureAtlasElementSliceData, one row
    // per sliced element; all zero here means the sprite is not sliced.
    Slice slice;
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
    // cap and then rails to the bottom, its right column (0.515625..1, the
    // rails 1 px right of the left column's) holds two bottom caps. Each cap
    // is 28 px, the member table's 31x28, and must fit the template's
    // steppers (Back y=-4 16 tall, Forward y=4 17 tall). Alpha measured: the
    // top cap at rows 0..28 has a 4-row border, a 16-row well and the
    // separator at rows 20..21. The upper bottom cap (rows 78..106, where the
    // Friends frame's TexCoords end) has only a 3-row border, so the Forward
    // stepper would cover its separator; the lower one at rows 227..255 has
    // a 4-row border, a 17-row well and the separator 21..22 rows up, the
    // stepper's exact fit. The center tiles vertically there; here it is the
    // whole rail below the top cap, so a stretched center shows the same
    // rails.
    {"UI-ClassicScrollBar-EndCap-Top",
     "Interface\\PaperDollInfoFrame\\UI-Character-ScrollBar", 31, 28, 0.0f,
     0.484375f, 0.0f, 0.109375f, 1941, 18426, false, false},
    {"UI-ClassicScrollBar-EndCap-Bottom",
     "Interface\\PaperDollInfoFrame\\UI-Character-ScrollBar", 31, 28, 0.515625f,
     1.0f, 0.88671875f, 0.99609375f, 1941, 18425, false, false},
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
    // WIDGET MATCH, transposed. Retail's WowTrimHorizontalScrollBar
    // (Shared/Scroll/TrimScrollBar.xml) draws with these atlases, and its
    // layout is the vertical bar's with x and y swapped; Classic Era has no
    // horizontal bar and this client no horizontal scroll bar art. Each is the
    // Classic scroll bar entry above transposed: the top cap is the left cap,
    // the bottom cap the right one, the rails the center (tiling horizontally,
    // as the `_` prefix and member flag 4 mark it), the up arrow the left one
    // and the down arrow the right one. Sizes are the transposed 1.12 sprites.
    {"UI-ScrollBar-EndCap-Left",
     "Interface\\PaperDollInfoFrame\\UI-Character-ScrollBar", 28, 31, 0.0f,
     0.484375f, 0.0f, 0.109375f, 1769, 17384, false, false, true},
    {"UI-ScrollBar-EndCap-Right",
     "Interface\\PaperDollInfoFrame\\UI-Character-ScrollBar", 28, 31, 0.515625f,
     1.0f, 0.88671875f, 0.99609375f, 1769, 17385, false, false, true},
    {"_UI-ScrollBar-Center",
     "Interface\\PaperDollInfoFrame\\UI-Character-ScrollBar", 228, 31, 0.0f,
     0.484375f, 0.109375f, 1.0f, 1770, 17418, true, false, true},
    {"UI-ScrollBar-ScrollLeftButton-Up",
     "Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-Up", 17, 19,
     0.1875f, 0.78125f, 0.21875f, 0.75f, 1769, 17406, false, false, true},
    {"UI-ScrollBar-ScrollLeftButton-Down",
     "Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-Down", 17, 19,
     0.1875f, 0.78125f, 0.21875f, 0.75f, 1769, 17404, false, false, true},
    {"UI-ScrollBar-ScrollLeftButton-Disabled",
     "Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-Disabled", 17, 19,
     0.1875f, 0.78125f, 0.21875f, 0.75f, 1769, 17403, false, false, true},
    {"UI-ScrollBar-ScrollLeftButton-Highlight",
     "Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-Highlight", 17, 19,
     0.1875f, 0.78125f, 0.21875f, 0.75f, 1769, 17405, false, false, true},
    {"UI-ScrollBar-ScrollRightButton-Up",
     "Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-Up", 17, 19,
     0.1875f, 0.78125f, 0.21875f, 0.75f, 1769, 17410, false, false, true},
    {"UI-ScrollBar-ScrollRightButton-Down",
     "Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-Down", 17, 19,
     0.1875f, 0.78125f, 0.21875f, 0.75f, 1769, 17408, false, false, true},
    {"UI-ScrollBar-ScrollRightButton-Disabled",
     "Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-Disabled", 17, 19,
     0.1875f, 0.78125f, 0.21875f, 0.75f, 1769, 17407, false, false, true},
    {"UI-ScrollBar-ScrollRightButton-Highlight",
     "Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-Highlight", 17, 19,
     0.1875f, 0.78125f, 0.21875f, 0.75f, 1769, 17409, false, false, true},
    // EMBEDDED ART. ModelSceneControlFrame's buttons and icons have no 1.12
    // art, so the DLL carries them (Texture::Builtin): the seven sprites cut
    // from retail's Interface\Common\CommonButtons (atlas 2666) and
    // CommonIcons (1563) at the member table's rects, which match retail's
    // sheets, and downscaled into one 128x128 sheet near their drawn size (the
    // buttons draw at 32, the icons at 16; a built-in texture has one mip).
    // Size is the member table's OverrideWidth/Height, the size retail lays
    // them out at.
    {"common-button-square-gray-up", "Interface\\Common\\ModelSceneControl", 42, 42, 0.0f,
     0.5f, 0.0f, 0.5f, 2666, 25907, false, false},
    {"common-button-square-gray-down", "Interface\\Common\\ModelSceneControl", 42, 42, 0.5f,
     1.0f, 0.0f, 0.5f, 2666, 25906, false, false},
    {"common-icon-zoomin", "Interface\\Common\\ModelSceneControl", 25, 25, 0.0f, 0.25f, 0.5f,
     0.75f, 1563, 11970, false, false},
    {"common-icon-zoomout", "Interface\\Common\\ModelSceneControl", 25, 25, 0.25f, 0.5f, 0.5f,
     0.75f, 1563, 11971, false, false},
    {"common-icon-rotateleft", "Interface\\Common\\ModelSceneControl", 20, 20, 0.5f, 0.75f,
     0.5f, 0.75f, 1563, 11967, false, false},
    {"common-icon-rotateright", "Interface\\Common\\ModelSceneControl", 20, 20, 0.75f, 1.0f,
     0.5f, 0.75f, 1563, 11968, false, false},
    {"common-icon-undo", "Interface\\Common\\ModelSceneControl", 25, 25, 0.0f, 0.25f, 0.75f,
     1.0f, 1563, 11969, false, false},
    // EMBEDDED ART. The damage meter (ClassicAPI_DamageMeter, a port of
    // Blizzard_DamageMeter) and its dropdowns draw retail art 1.12 has none of,
    // so the DLL carries it (Texture::Builtin) in two sheets: the window,
    // bars and buttons in DamageMeter, the class, elite, rare and faction
    // icons in DamageMeterIcons (its own sheet so each keeps a 256-colour
    // palette). Each sprite is cut from the retail sheet named after its row
    // at the rect of the _forever_ build's UiTextureAtlasMember (1.60.1.70959,
    // the build the art came from; the live build's coordinates differ, e.g.
    // ui-damagemeters-header-bar is 263 wide there and 280 here, and
    // ObjectIconsAtlas was repacked), with a 1-pixel extruded border so
    // filtering never reaches a neighbour. The class icons are cut down from
    // 128 to 64 and the elite icons from 80 to 32 (drawn at bar and text
    // height). Size is OverrideWidth/Height when set, else Width/Height: the
    // size retail lays the sprite out at. IDs are that build's atlas and
    // element IDs. The slice data is that build's
    // UiTextureAtlasElementSliceData (wago.tools export: Left, Top, Right,
    // Bottom, SliceMode), matched to these rows by UiTextureAtlasElementID.
    {"ui-damagemeters-header-bar", "Interface\\DamageMeter\\DamageMeter", 280, 28, 0.322265625f, 0.869140625f, 0.58984375f, 0.69921875f, 3658, 32444, false, false, false, {80, 14, 80, 22, 0}}, // interface/hud/uidamagemeters
    {"damagemeters-background", "Interface\\DamageMeter\\DamageMeter", 154, 148, 0.001953125f, 0.302734375f, 0.00390625f, 0.58203125f, 3658, 32562, false, false, false, {20, 80, 20, 20, 0}}, // interface/hud/uidamagemeters
    {"damagemeters-scalehandle", "Interface\\DamageMeter\\DamageMeter", 60, 60, 0.443359375f, 0.560546875f, 0.00390625f, 0.23828125f, 3658, 32492, false, false}, // interface/hud/uidamagemeters
    {"damagemeters-scalehandle-hover", "Interface\\DamageMeter\\DamageMeter", 60, 60, 0.564453125f, 0.681640625f, 0.00390625f, 0.23828125f, 3658, 32563, false, false}, // interface/hud/uidamagemeters
    {"damagemeters-scalehandle-pressed", "Interface\\DamageMeter\\DamageMeter", 60, 60, 0.685546875f, 0.802734375f, 0.00390625f, 0.23828125f, 3658, 32564, false, false}, // interface/hud/uidamagemeters
    {"ui-hud-cooldownmanager-bar", "Interface\\DamageMeter\\DamageMeter", 124, 10, 0.255859375f, 0.498046875f, 0.86328125f, 0.90234375f, 3147, 30193, false, false, false, {4, 5, 4, 4, 0}}, // interface/hud/uicooldownmanager
    {"ui-hud-cooldownmanager-bar-bg", "Interface\\DamageMeter\\DamageMeter", 132, 19, 0.228515625f, 0.486328125f, 0.75f, 0.82421875f, 3147, 30191, false, false, false, {121, 7, 10, 11, 1}}, // interface/hud/uicooldownmanager
    {"ui-damagemeters-bar-shadowbg", "Interface\\DamageMeter\\DamageMeter", 128, 14, 0.685546875f, 0.935546875f, 0.75f, 0.8046875f, 3658, 32496, false, false, false, {24, 10, 24, 10, 0}}, // interface/hud/uidamagemeters
    {"ui-damagemeters-bar-shadowedge", "Interface\\DamageMeter\\DamageMeter", 128, 14, 0.001953125f, 0.251953125f, 0.86328125f, 0.91796875f, 3658, 32443, false, false, false, {4, 5, 4, 4, 0}}, // interface/hud/uidamagemeters
    {"ui-questtrackerbutton-collapse-all", "Interface\\DamageMeter\\DamageMeter", 18, 19, 0.490234375f, 0.525390625f, 0.75f, 0.82421875f, 2547, 12246, false, false}, // interface/questframe/questtracker
    {"ui-questtrackerbutton-collapse-all-pressed", "Interface\\DamageMeter\\DamageMeter", 18, 19, 0.529296875f, 0.564453125f, 0.75f, 0.82421875f, 2547, 12245, false, false}, // interface/questframe/questtracker
    {"ui-questtrackerbutton-expand-all", "Interface\\DamageMeter\\DamageMeter", 18, 19, 0.568359375f, 0.603515625f, 0.75f, 0.82421875f, 2547, 12250, false, false}, // interface/questframe/questtracker
    {"ui-questtrackerbutton-expand-all-pressed", "Interface\\DamageMeter\\DamageMeter", 18, 19, 0.607421875f, 0.642578125f, 0.75f, 0.82421875f, 2547, 12249, false, false}, // interface/questframe/questtracker
    {"ui-questtrackerbutton-red-highlight", "Interface\\DamageMeter\\DamageMeter", 18, 19, 0.646484375f, 0.681640625f, 0.75f, 0.82421875f, 2547, 24977, false, false}, // interface/questframe/questtracker
    {"common-dropdown-bg", "Interface\\DamageMeter\\DamageMeter", 68, 68, 0.306640625f, 0.439453125f, 0.00390625f, 0.26953125f, 2634, 25557, false, false, false, {16, 13, 16, 19, 0}}, // interface/common/commondropdown
    {"common-dropdown-a-button-settings-disabled-shadowless", "Interface\\DamageMeter\\DamageMeter", 27, 27, 0.873046875f, 0.92578125f, 0.58984375f, 0.6953125f, 3678, 32612, false, false}, // interface/common/commondropdownsettings
    {"common-dropdown-a-button-settings-shadowless", "Interface\\DamageMeter\\DamageMeter", 27, 27, 0.9296875f, 0.982421875f, 0.58984375f, 0.6953125f, 3678, 32617, false, false}, // interface/common/commondropdownsettings
    {"common-dropdown-a-button-settings-pressed-shadowless", "Interface\\DamageMeter\\DamageMeter", 27, 27, 0.001953125f, 0.0546875f, 0.75f, 0.85546875f, 3678, 32615, false, false}, // interface/common/commondropdownsettings
    {"common-dropdown-a-button-settings-hover-shadowless", "Interface\\DamageMeter\\DamageMeter", 27, 27, 0.05859375f, 0.111328125f, 0.75f, 0.85546875f, 3678, 32613, false, false}, // interface/common/commondropdownsettings
    {"common-dropdown-a-button-settings-pressedhover-shadowless", "Interface\\DamageMeter\\DamageMeter", 27, 27, 0.115234375f, 0.16796875f, 0.75f, 0.85546875f, 3678, 32616, false, false}, // interface/common/commondropdownsettings
    {"common-dropdown-a-button-settings-open-shadowless", "Interface\\DamageMeter\\DamageMeter", 27, 27, 0.171875f, 0.224609375f, 0.75f, 0.85546875f, 3678, 32614, false, false}, // interface/common/commondropdownsettings
    {"common-dropdown-c-button", "Interface\\DamageMeter\\DamageMeter", 39, 39, 0.806640625f, 0.8828125f, 0.00390625f, 0.15625f, 2634, 25502, false, false, false, {12, 0, 12, 0, 0}}, // interface/common/commondropdown
    {"common-dropdown-c-button-hover-1", "Interface\\DamageMeter\\DamageMeter", 39, 39, 0.88671875f, 0.962890625f, 0.00390625f, 0.15625f, 2634, 25728, false, false, false, {12, 0, 12, 0, 0}}, // interface/common/commondropdown
    {"common-dropdown-c-button-pressed-1", "Interface\\DamageMeter\\DamageMeter", 39, 39, 0.001953125f, 0.078125f, 0.58984375f, 0.7421875f, 2634, 25730, false, false, false, {14, 0, 12, 0, 0}}, // interface/common/commondropdown
    {"common-dropdown-c-button-pressedhover-1", "Interface\\DamageMeter\\DamageMeter", 39, 39, 0.08203125f, 0.158203125f, 0.58984375f, 0.7421875f, 2634, 25732, false, false, false, {14, 0, 12, 0, 0}}, // interface/common/commondropdown
    {"common-dropdown-c-button-open", "Interface\\DamageMeter\\DamageMeter", 39, 39, 0.162109375f, 0.23828125f, 0.58984375f, 0.7421875f, 2634, 25513, false, false, false, {12, 0, 12, 0, 0}}, // interface/common/commondropdown
    {"common-dropdown-c-button-disabled", "Interface\\DamageMeter\\DamageMeter", 39, 39, 0.2421875f, 0.318359375f, 0.58984375f, 0.7421875f, 2634, 25591, false, false, false, {12, 0, 12, 0, 0}}, // interface/common/commondropdown
    {"common-dropdown-c-button-hover-arrow", "Interface\\DamageMeter\\DamageMeter", 12, 5, 0.501953125f, 0.525390625f, 0.86328125f, 0.8828125f, 2634, 25565, false, false}, // interface/common/commondropdown
    {"classicon-warrior", "Interface\\DamageMeter\\DamageMeterIcons", 128, 128, 0.00390625f, 0.25390625f, 0.00390625f, 0.25390625f, 897, 12069, false, false}, // interface/glues/charactercreate/charactercreateicons
    {"classicon-paladin", "Interface\\DamageMeter\\DamageMeterIcons", 128, 128, 0.26171875f, 0.51171875f, 0.00390625f, 0.25390625f, 897, 12064, false, false}, // interface/glues/charactercreate/charactercreateicons
    {"classicon-hunter", "Interface\\DamageMeter\\DamageMeterIcons", 128, 128, 0.51953125f, 0.76953125f, 0.00390625f, 0.25390625f, 897, 12061, false, false}, // interface/glues/charactercreate/charactercreateicons
    {"classicon-rogue", "Interface\\DamageMeter\\DamageMeterIcons", 128, 128, 0.00390625f, 0.25390625f, 0.26171875f, 0.51171875f, 897, 12066, false, false}, // interface/glues/charactercreate/charactercreateicons
    {"classicon-priest", "Interface\\DamageMeter\\DamageMeterIcons", 128, 128, 0.26171875f, 0.51171875f, 0.26171875f, 0.51171875f, 897, 12065, false, false}, // interface/glues/charactercreate/charactercreateicons
    {"classicon-shaman", "Interface\\DamageMeter\\DamageMeterIcons", 128, 128, 0.51953125f, 0.76953125f, 0.26171875f, 0.51171875f, 897, 12067, false, false}, // interface/glues/charactercreate/charactercreateicons
    {"classicon-mage", "Interface\\DamageMeter\\DamageMeterIcons", 128, 128, 0.00390625f, 0.25390625f, 0.51953125f, 0.76953125f, 897, 12062, false, false}, // interface/glues/charactercreate/charactercreateicons
    {"classicon-warlock", "Interface\\DamageMeter\\DamageMeterIcons", 128, 128, 0.26171875f, 0.51171875f, 0.51953125f, 0.76953125f, 897, 12068, false, false}, // interface/glues/charactercreate/charactercreateicons
    {"classicon-druid", "Interface\\DamageMeter\\DamageMeterIcons", 128, 128, 0.51953125f, 0.76953125f, 0.51953125f, 0.76953125f, 897, 12060, false, false}, // interface/glues/charactercreate/charactercreateicons
    {"nameplates-icon-elite-gold", "Interface\\DamageMeter\\DamageMeterIcons", 40, 40, 0.77734375f, 0.90234375f, 0.51953125f, 0.64453125f, 731, 5444, false, false}, // interface/targetingframe/nameplates
    {"nameplates-icon-elite-silver", "Interface\\DamageMeter\\DamageMeterIcons", 40, 40, 0.00390625f, 0.12890625f, 0.77734375f, 0.90234375f, 731, 5470, false, false}, // interface/targetingframe/nameplates
    {"ui-hud-unitframe-target-portraiton-boss-rare-star", "Interface\\DamageMeter\\DamageMeterIcons", 20, 20, 0.40234375f, 0.50390625f, 0.77734375f, 0.87890625f, 2130, 20547, false, false}, // interface/hud/uiunitframeboss
    {"hordesymbol", "Interface\\DamageMeter\\DamageMeterIcons", 32, 32, 0.13671875f, 0.26171875f, 0.77734375f, 0.90234375f, 647, 6902, false, false}, // interface/minimap/objecticonsatlas
    {"alliancesymbol", "Interface\\DamageMeter\\DamageMeterIcons", 32, 32, 0.26953125f, 0.39453125f, 0.77734375f, 0.90234375f, 647, 6901, false, false}, // interface/minimap/objecticonsatlas
    // EMBEDDED ART. The NineSlice layouts the ported panel templates use
    // (PortraitFrameTemplate, ButtonFrameTemplateNoPortrait and their
    // Minimizable variants, SimplePanelTemplate, InsetFrameTemplate) and the
    // corners of FlatPanelBackgroundTemplate, cut from the _forever_ build's
    // sheets as the damage meter's are. The layouts name the 1x atlases; this
    // build has the metal art only as -2x, which is cut down to the 1x size
    // retail lays it out at (the corner is 75 wide: the portrait ring at the
    // template's -5,7 sits in the -13,16 corner only at that size). A tiling
    // edge repeats only when it spans its file on the tiled axis
    // (Texture::Atlas::Repeats), so each tile width or height has its own
    // file, padded on the other axis alone and resampled with its neighbours
    // so the seam stays continuous; the corners share one sheet.
    {"UI-Frame-PortraitMetal-CornerTopLeft", "Interface\\FrameGeneral\\UIFrameMetal", 75, 75, 0.00390625f, 0.296875f, 0.00390625f, 0.296875f, 1390, 7672, false, false}, // interface/framegeneral/uiframemetal2x
    {"UI-Frame-Metal-CornerTopLeft", "Interface\\FrameGeneral\\UIFrameMetal", 75, 75, 0.3046875f, 0.59765625f, 0.00390625f, 0.296875f, 1390, 7678, false, false}, // interface/framegeneral/uiframemetal2x
    {"UI-Frame-Metal-CornerTopRight", "Interface\\FrameGeneral\\UIFrameMetal", 75, 75, 0.60546875f, 0.8984375f, 0.00390625f, 0.296875f, 1390, 7679, false, false}, // interface/framegeneral/uiframemetal2x
    {"UI-Frame-Metal-CornerTopRightDouble", "Interface\\FrameGeneral\\UIFrameMetal", 75, 75, 0.00390625f, 0.296875f, 0.3046875f, 0.59765625f, 1390, 7715, false, false}, // interface/framegeneral/uiframemetal2x
    {"UI-Frame-Metal-CornerBottomLeft", "Interface\\FrameGeneral\\UIFrameMetal", 32, 32, 0.5625f, 0.6875f, 0.3046875f, 0.4296875f, 1390, 7674, false, false}, // interface/framegeneral/uiframemetal2x
    {"UI-Frame-Metal-CornerBottomRight", "Interface\\FrameGeneral\\UIFrameMetal", 32, 32, 0.6953125f, 0.8203125f, 0.3046875f, 0.4296875f, 1390, 7677, false, false}, // interface/framegeneral/uiframemetal2x
    {"UI-Frame-SimpleMetal-CornerTopLeft", "Interface\\FrameGeneral\\UIFrameMetal", 64, 64, 0.3046875f, 0.5546875f, 0.3046875f, 0.5546875f, 1380, 7682, false, false}, // interface/framegeneral/uiframesimplemetal
    {"UI-Frame-InnerTopLeft", "Interface\\FrameGeneral\\UIFrameMetal", 6, 6, 0.96875f, 0.9921875f, 0.3046875f, 0.328125f, 948, 6379, false, false}, // interface/interface/framegeneral/uiframe
    {"UI-Frame-InnerTopRight", "Interface\\FrameGeneral\\UIFrameMetal", 6, 6, 0.00390625f, 0.02734375f, 0.60546875f, 0.62890625f, 948, 6389, false, false}, // interface/interface/framegeneral/uiframe
    {"UI-Frame-InnerBotLeftCorner", "Interface\\FrameGeneral\\UIFrameMetal", 6, 6, 0.03515625f, 0.05859375f, 0.60546875f, 0.62890625f, 948, 6381, false, false}, // interface/interface/framegeneral/uiframe
    {"UI-Frame-InnerBotRight", "Interface\\FrameGeneral\\UIFrameMetal", 6, 6, 0.06640625f, 0.08984375f, 0.60546875f, 0.62890625f, 948, 6388, false, false}, // interface/interface/framegeneral/uiframe
    {"uiframebackground-nineslice-cornerbottomleft", "Interface\\FrameGeneral\\UIFrameMetal", 16, 16, 0.828125f, 0.890625f, 0.3046875f, 0.3671875f, 2120, 20550, false, false}, // interface/framegeneral/uiframebackground
    {"uiframebackground-nineslice-cornerbottomright", "Interface\\FrameGeneral\\UIFrameMetal", 16, 16, 0.8984375f, 0.9609375f, 0.3046875f, 0.3671875f, 2120, 20551, false, false}, // interface/framegeneral/uiframebackground
    {"_UI-Frame-Metal-EdgeTop", "Interface\\FrameGeneral\\UIFrameMetalEdgeTop", 32, 75, 0.0f, 1.0f, 0.0078125f, 0.59375f, 1395, 7676, true, false}, // interface/framegeneral/uiframemetalhorizontal2x
    {"_UI-Frame-Metal-EdgeBottom", "Interface\\FrameGeneral\\UIFrameMetalEdgeBottom", 16, 32, 0.0f, 1.0f, 0.015625f, 0.515625f, 1395, 7673, true, false}, // interface/framegeneral/uiframemetalhorizontal2x
    {"!UI-Frame-Metal-EdgeLeft", "Interface\\FrameGeneral\\UIFrameMetalEdgeSides", 75, 16, 0.00390625f, 0.296875f, 0.0f, 1.0f, 1394, 7680, false, true}, // interface/framegeneral/uiframemetalvertical2x
    {"!UI-Frame-Metal-EdgeRight", "Interface\\FrameGeneral\\UIFrameMetalEdgeSides", 75, 16, 0.3046875f, 0.59765625f, 0.0f, 1.0f, 1394, 7681, false, true}, // interface/framegeneral/uiframemetalvertical2x
    {"_UI-Frame-SimpleMetal-EdgeTop", "Interface\\FrameGeneral\\UIFrameSimpleMetalEdgeTop", 64, 64, 0.0f, 1.0f, 0.0078125f, 0.5078125f, 1380, 7683, true, false}, // interface/framegeneral/uiframesimplemetal
    {"!UI-Frame-SimpleMetal-EdgeLeft", "Interface\\FrameGeneral\\UIFrameSimpleMetalEdgeLeft", 64, 64, 0.0078125f, 0.5078125f, 0.0f, 1.0f, 1381, 7684, false, true}, // interface/framegeneral/uiframesimplemetalvertical
    {"_UI-Frame-InnerTopTile", "Interface\\FrameGeneral\\UIFrameInsetEdgeTopBottom", 256, 3, 0.0f, 1.0f, 0.0625f, 0.25f, 950, 6399, true, false}, // interface/framegeneral/uiframehorizontal
    {"_UI-Frame-InnerBotTile", "Interface\\FrameGeneral\\UIFrameInsetEdgeTopBottom", 256, 3, 0.0f, 1.0f, 0.375f, 0.5625f, 950, 6402, true, false}, // interface/framegeneral/uiframehorizontal
    {"!UI-Frame-InnerLeftTile", "Interface\\FrameGeneral\\UIFrameInsetEdgeSides", 3, 256, 0.0625f, 0.25f, 0.0f, 1.0f, 949, 6378, false, true}, // interface/framegeneral/uiframevertical
    {"!UI-Frame-InnerRightTile", "Interface\\FrameGeneral\\UIFrameInsetEdgeSides", 3, 256, 0.375f, 0.5625f, 0.0f, 1.0f, 949, 6396, false, true}, // interface/framegeneral/uiframevertical
    // EMBEDDED ART. The Mainline menu and dropdown art Blizzard_Menu draws
    // (Mainline/MenuTemplates, MenuVariants, MenuConstants), the menu row
    // hover (DarkMenuElementTemplate) and the stepper icons, cut from the
    // _forever_ build's sheets as the damage meter's are, into one sheet.
    // The damage meter's sheet already holds common-dropdown-bg and the
    // WowStyle2 button states above; these are the rest. Slice data is that
    // build's UiTextureAtlasElementSliceData.
    {"common-dropdown-a-button", "Interface\\Common\\CommonDropdown", 27, 27, 0.53125f, 0.583984375f, 0.00390625f, 0.109375f, 2634, 25556, false, false}, // interface/common/commondropdown
    {"common-dropdown-a-button-disabled", "Interface\\Common\\CommonDropdown", 27, 27, 0.587890625f, 0.640625f, 0.00390625f, 0.109375f, 2634, 25588, false, false}, // interface/common/commondropdown
    {"common-dropdown-a-button-disabled-shadowless", "Interface\\Common\\CommonDropdown", 27, 27, 0.64453125f, 0.697265625f, 0.00390625f, 0.109375f, 2634, 29007, false, false}, // interface/common/commondropdown
    {"common-dropdown-a-button-hover", "Interface\\Common\\CommonDropdown", 27, 27, 0.701171875f, 0.75390625f, 0.00390625f, 0.109375f, 2634, 25553, false, false}, // interface/common/commondropdown
    {"common-dropdown-a-button-hover-shadowless", "Interface\\Common\\CommonDropdown", 27, 27, 0.7578125f, 0.810546875f, 0.00390625f, 0.109375f, 2634, 29008, false, false}, // interface/common/commondropdown
    {"common-dropdown-a-button-open", "Interface\\Common\\CommonDropdown", 27, 27, 0.814453125f, 0.8671875f, 0.00390625f, 0.109375f, 2634, 25559, false, false}, // interface/common/commondropdown
    {"common-dropdown-a-button-open-shadowless", "Interface\\Common\\CommonDropdown", 27, 27, 0.87109375f, 0.923828125f, 0.00390625f, 0.109375f, 2634, 29009, false, false}, // interface/common/commondropdown
    {"common-dropdown-a-button-pressed", "Interface\\Common\\CommonDropdown", 27, 27, 0.927734375f, 0.98046875f, 0.00390625f, 0.109375f, 2634, 25554, false, false}, // interface/common/commondropdown
    {"common-dropdown-a-button-pressed-shadowless", "Interface\\Common\\CommonDropdown", 27, 27, 0.001953125f, 0.0546875f, 0.36328125f, 0.46875f, 2634, 29010, false, false}, // interface/common/commondropdown
    {"common-dropdown-a-button-pressedhover", "Interface\\Common\\CommonDropdown", 27, 27, 0.05859375f, 0.111328125f, 0.36328125f, 0.46875f, 2634, 25555, false, false}, // interface/common/commondropdown
    {"common-dropdown-a-button-pressedhover-shadowless", "Interface\\Common\\CommonDropdown", 27, 27, 0.115234375f, 0.16796875f, 0.36328125f, 0.46875f, 2634, 29011, false, false}, // interface/common/commondropdown
    {"common-dropdown-a-button-shadowless", "Interface\\Common\\CommonDropdown", 27, 27, 0.171875f, 0.224609375f, 0.36328125f, 0.46875f, 2634, 29012, false, false}, // interface/common/commondropdown
    {"common-dropdown-b-button", "Interface\\Common\\CommonDropdown", 97, 26, 0.228515625f, 0.41796875f, 0.36328125f, 0.46484375f, 2634, 25551, false, false, false, {8, 0, 18, 0, 0}}, // interface/common/commondropdown
    {"common-dropdown-b-button-disabled", "Interface\\Common\\CommonDropdown", 97, 26, 0.421875f, 0.611328125f, 0.36328125f, 0.46484375f, 2634, 25589, false, false, false, {8, 0, 18, 0, 0}}, // interface/common/commondropdown
    {"common-dropdown-b-button-hover", "Interface\\Common\\CommonDropdown", 97, 26, 0.615234375f, 0.8046875f, 0.36328125f, 0.46484375f, 2634, 25548, false, false, false, {8, 0, 18, 0, 0}}, // interface/common/commondropdown
    {"common-dropdown-b-button-open", "Interface\\Common\\CommonDropdown", 97, 26, 0.80859375f, 0.998046875f, 0.36328125f, 0.46484375f, 2634, 25560, false, false, false, {8, 0, 18, 0, 0}}, // interface/common/commondropdown
    {"common-dropdown-b-button-pressed", "Interface\\Common\\CommonDropdown", 97, 26, 0.001953125f, 0.19140625f, 0.4765625f, 0.578125f, 2634, 25549, false, false, false, {10, 1, 17, 1, 0}}, // interface/common/commondropdown
    {"common-dropdown-b-button-pressedhover", "Interface\\Common\\CommonDropdown", 97, 26, 0.1953125f, 0.384765625f, 0.4765625f, 0.578125f, 2634, 25550, false, false, false, {10, 0, 17, 0, 0}}, // interface/common/commondropdown
    {"common-dropdown-c-bg", "Interface\\Common\\CommonDropdown", 90, 90, 0.001953125f, 0.177734375f, 0.00390625f, 0.35546875f, 2634, 25590, false, false, false, {23, 18, 23, 28, 0}}, // interface/common/commondropdown
    {"common-dropdown-c-button-hover-2", "Interface\\Common\\CommonDropdown", 39, 39, 0.291015625f, 0.3671875f, 0.00390625f, 0.15625f, 2634, 25729, false, false, false, {12, 0, 12, 0, 0}}, // interface/common/commondropdown
    {"common-dropdown-c-button-pressed-2", "Interface\\Common\\CommonDropdown", 39, 39, 0.37109375f, 0.447265625f, 0.00390625f, 0.15625f, 2634, 25731, false, false, false, {14, 0, 12, 0, 0}}, // interface/common/commondropdown
    {"common-dropdown-c-button-pressedhover-2", "Interface\\Common\\CommonDropdown", 39, 39, 0.451171875f, 0.52734375f, 0.00390625f, 0.15625f, 2634, 25733, false, false, false, {14, 0, 12, 0, 0}}, // interface/common/commondropdown
    {"common-dropdown-icon-checkmark-yellow", "Interface\\Common\\CommonDropdown", 15, 14, 0.708984375f, 0.73828125f, 0.4765625f, 0.53125f, 2634, 25561, false, false}, // interface/common/commondropdown
    {"common-dropdown-icon-radialtick-yellow", "Interface\\Common\\CommonDropdown", 18, 18, 0.482421875f, 0.517578125f, 0.4765625f, 0.546875f, 2634, 25562, false, false}, // interface/common/commondropdown
    {"common-dropdown-textholder", "Interface\\Common\\CommonDropdown", 54, 41, 0.181640625f, 0.287109375f, 0.00390625f, 0.1640625f, 2634, 25558, false, false, false, {16, 0, 19, 0, 0}}, // interface/common/commondropdown
    {"common-dropdown-tickradial", "Interface\\Common\\CommonDropdown", 18, 18, 0.521484375f, 0.556640625f, 0.4765625f, 0.546875f, 2634, 25563, false, false}, // interface/common/commondropdown
    {"common-dropdown-ticksquare", "Interface\\Common\\CommonDropdown", 12, 12, 0.7421875f, 0.765625f, 0.4765625f, 0.5234375f, 2634, 25564, false, false}, // interface/common/commondropdown
    {"common-search-magnifyingglass", "Interface\\Common\\CommonDropdown", 24, 24, 0.388671875f, 0.435546875f, 0.4765625f, 0.5703125f, 3172, 11711, false, false}, // interface/shop/catalogshop
    {"common-dropdown-customize-mouseover", "Interface\\Common\\CommonDropdown", 20, 20, 0.439453125f, 0.478515625f, 0.4765625f, 0.5546875f, 2634, 25927, false, false, false, {4, 4, 4, 4, 0}}, // interface/common/commondropdown
    {"common-dropdown-icon-next", "Interface\\Common\\CommonDropdown", 17, 17, 0.560546875f, 0.59375f, 0.4765625f, 0.54296875f, 2634, 25737, false, false}, // interface/common/commondropdown
    {"common-dropdown-icon-next-disabled", "Interface\\Common\\CommonDropdown", 17, 17, 0.59765625f, 0.630859375f, 0.4765625f, 0.54296875f, 2634, 25736, false, false}, // interface/common/commondropdown
    {"common-dropdown-icon-back", "Interface\\Common\\CommonDropdown", 17, 17, 0.634765625f, 0.66796875f, 0.4765625f, 0.54296875f, 2634, 25735, false, false}, // interface/common/commondropdown
    {"common-dropdown-icon-back-disabled", "Interface\\Common\\CommonDropdown", 17, 17, 0.671875f, 0.705078125f, 0.4765625f, 0.54296875f, 2634, 25734, false, false}, // interface/common/commondropdown
    // EMBEDDED ART, retail's own sheets. The Settings window (Settings\) and
    // the shared controls it draws (MinimalTab, MinimalSlider, MinimalScrollBar,
    // the search box, the panel close button) take their art from these
    // _forever_ (1.60.1.70959) sheets. Each sheet ships whole at its retail
    // game path, re-encoded as a palettized BLP (1.12's BLP reader has no DXT5
    // converter), so every row is the member table's own rect on it, with no
    // repacking. The sheets carry their own 1-pixel gaps; the 1-pixel tiling
    // middles stretch, which draws the same as a repeat. The clear button is
    // named on two sheets: this is the catalog shop's (the higher atlas ID,
    // the sheet the magnifying glass above was cut from), cut alone into a
    // small file because that sheet is 2048 wide. The track middle is also on
    // a catalog shop sheet, with identical pixels.
    {"Options_InnerFrame", "Interface\\OptionsFrame\\Options", 886, 618, 0.0009765625f, 0.8662109375f, 0.146484375f, 0.75f, 733, 18651, false, false}, // interface/optionsframe/options
    {"Options_HorizontalDivider", "Interface\\OptionsFrame\\Options", 630, 1, 0.0009765625f, 0.6162109375f, 0.1435546875f, 0.14453125f, 733, 18647, false, false}, // interface/optionsframe/options
    {"Options_List_Active", "Interface\\OptionsFrame\\Options", 187, 21, 0.58984375f, 0.7724609375f, 0.0009765625f, 0.021484375f, 733, 18648, false, false}, // interface/optionsframe/options
    {"Options_List_Hover", "Interface\\OptionsFrame\\Options", 187, 21, 0.7744140625f, 0.95703125f, 0.0009765625f, 0.021484375f, 733, 18649, false, false}, // interface/optionsframe/options
    {"Options_CategoryHeader_1", "Interface\\OptionsFrame\\Options", 199, 144, 0.0009765625f, 0.1953125f, 0.0009765625f, 0.1416015625f, 733, 18644, false, false}, // interface/optionsframe/options
    {"Options_CategoryHeader_2", "Interface\\OptionsFrame\\Options", 199, 144, 0.3935546875f, 0.587890625f, 0.0009765625f, 0.1416015625f, 733, 18645, false, false}, // interface/optionsframe/options
    {"Options_CategoryHeader_3", "Interface\\OptionsFrame\\Options", 199, 144, 0.197265625f, 0.3916015625f, 0.0009765625f, 0.1416015625f, 733, 18646, false, false}, // interface/optionsframe/options
    {"Options_Tab_Left", "Interface\\OptionsFrame\\Options", 7, 23, 0.5927734375f, 0.599609375f, 0.05078125f, 0.0732421875f, 733, 18671, false, false}, // interface/optionsframe/options
    {"Options_Tab_Middle", "Interface\\OptionsFrame\\Options", 1, 23, 0.58984375f, 0.5908203125f, 0.0234375f, 0.0458984375f, 733, 18672, false, false}, // interface/optionsframe/options
    {"Options_Tab_Right", "Interface\\OptionsFrame\\Options", 7, 23, 0.5927734375f, 0.599609375f, 0.1025390625f, 0.125f, 733, 18673, false, false}, // interface/optionsframe/options
    {"Options_Tab_Active_Left", "Interface\\OptionsFrame\\Options", 7, 26, 0.5927734375f, 0.599609375f, 0.0234375f, 0.048828125f, 733, 18666, false, false}, // interface/optionsframe/options
    {"Options_Tab_Active_Middle", "Interface\\OptionsFrame\\Options", 1, 26, 0.58984375f, 0.5908203125f, 0.0478515625f, 0.0732421875f, 733, 18667, false, false}, // interface/optionsframe/options
    {"Options_Tab_Active_Right", "Interface\\OptionsFrame\\Options", 7, 26, 0.5927734375f, 0.599609375f, 0.0751953125f, 0.1005859375f, 733, 18668, false, false}, // interface/optionsframe/options
    {"checkbox-minimal", "Interface\\Common\\MinimalCheckbox", 30, 29, 0.015625f, 0.484375f, 0.015625f, 0.46875f, 1983, 18779, false, false}, // interface/common/minimalcheckbox
    {"checkmark-minimal", "Interface\\Common\\MinimalCheckbox", 30, 29, 0.015625f, 0.484375f, 0.5f, 0.953125f, 1983, 18781, false, false}, // interface/common/minimalcheckbox
    {"checkmark-minimal-disabled", "Interface\\Common\\MinimalCheckbox", 30, 29, 0.515625f, 0.984375f, 0.015625f, 0.46875f, 1983, 18780, false, false}, // interface/common/minimalcheckbox
    {"Options_ListExpand_Left", "Interface\\Options\\OptionsExpandListButton", 12, 26, 0.0078125f, 0.1015625f, 0.65625f, 0.859375f, 1974, 18661, false, false}, // interface/options/optionsexpandlistbutton
    {"Options_ListExpand_Right", "Interface\\Options\\OptionsExpandListButton", 28, 26, 0.0078125f, 0.2265625f, 0.4375f, 0.640625f, 1974, 18662, false, false}, // interface/options/optionsexpandlistbutton
    {"Options_ListExpand_Right_Expanded", "Interface\\Options\\OptionsExpandListButton", 28, 26, 0.2421875f, 0.4609375f, 0.4375f, 0.640625f, 1974, 18659, false, false}, // interface/options/optionsexpandlistbutton
    {"_Options_ListExpand_Middle", "Interface\\Options\\OptionsExpandListButton", 1, 26, 0.0f, 0.0078125f, 0.21875f, 0.421875f, 1974, 18660, true, false}, // interface/options/optionsexpandlistbutton
    {"common-button-dropdown-closed", "Interface\\Common\\CommonButtonsDropdown", 22, 22, 0.015625f, 0.359375f, 0.015625f, 0.359375f, 1938, 18374, false, false}, // interface/common/commonbuttonsdropdown
    {"common-button-dropdown-closedpressed", "Interface\\Common\\CommonButtonsDropdown", 22, 22, 0.390625f, 0.734375f, 0.015625f, 0.359375f, 1938, 18375, false, false}, // interface/common/commonbuttonsdropdown
    {"common-button-dropdown-open", "Interface\\Common\\CommonButtonsDropdown", 22, 22, 0.015625f, 0.359375f, 0.390625f, 0.734375f, 1938, 18376, false, false}, // interface/common/commonbuttonsdropdown
    {"common-button-dropdown-openpressed", "Interface\\Common\\CommonButtonsDropdown", 22, 22, 0.390625f, 0.734375f, 0.390625f, 0.734375f, 1938, 18377, false, false}, // interface/common/commonbuttonsdropdown
    {"Minimal_SliderBar_Left", "Interface\\Buttons\\MinimalSliderBar", 11, 17, 0.4375f, 0.78125f, 0.3203125f, 0.453125f, 1973, 18655, false, false}, // interface/buttons/minimalsliderbar
    {"Minimal_SliderBar_Right", "Interface\\Buttons\\MinimalSliderBar", 11, 17, 0.03125f, 0.375f, 0.484375f, 0.6171875f, 1973, 18656, false, false}, // interface/buttons/minimalsliderbar
    {"_Minimal_SliderBar_Middle", "Interface\\Buttons\\MinimalSliderBar", 1, 17, 0.0f, 0.03125f, 0.0078125f, 0.140625f, 1973, 18657, true, false}, // interface/buttons/minimalsliderbar
    {"Minimal_SliderBar_Button", "Interface\\Buttons\\MinimalSliderBar", 20, 19, 0.03125f, 0.65625f, 0.15625f, 0.3046875f, 1973, 18652, false, false}, // interface/buttons/minimalsliderbar
    {"Minimal_SliderBar_Button_Left", "Interface\\Buttons\\MinimalSliderBar", 11, 19, 0.03125f, 0.375f, 0.3203125f, 0.46875f, 1973, 18653, false, false}, // interface/buttons/minimalsliderbar
    {"Minimal_SliderBar_Button_Right", "Interface\\Buttons\\MinimalSliderBar", 9, 18, 0.03125f, 0.3125f, 0.6328125f, 0.7734375f, 1973, 18654, false, false}, // interface/buttons/minimalsliderbar
    {"RedButton-Exit", "Interface\\Buttons\\RedButtons", 18, 19, 0.1640625f, 0.3046875f, 0.015625f, 0.3125f, 2196, 20574, false, false}, // interface/buttons/redbuttons
    {"RedButton-exit-pressed", "Interface\\Buttons\\RedButtons", 18, 19, 0.1640625f, 0.3046875f, 0.671875f, 0.96875f, 2196, 20576, false, false}, // interface/buttons/redbuttons
    {"RedButton-Exit-Disabled", "Interface\\Buttons\\RedButtons", 18, 19, 0.1640625f, 0.3046875f, 0.34375f, 0.640625f, 2196, 20575, false, false}, // interface/buttons/redbuttons
    {"RedButton-Highlight", "Interface\\Buttons\\RedButtons", 18, 19, 0.4765625f, 0.6171875f, 0.015625f, 0.3125f, 2196, 20580, false, false}, // interface/buttons/redbuttons
    {"minimal-scrollbar-arrow-top", "Interface\\Buttons\\MinimalScrollbarProportional", 17, 11, 0.6875f, 0.8203125f, 0.015625f, 0.1875f, 1921, 18212, false, false}, // interface/buttons/minimalscrollbarproportional
    {"minimal-scrollbar-arrow-top-over", "Interface\\Buttons\\MinimalScrollbarProportional", 17, 11, 0.390625f, 0.5234375f, 0.21875f, 0.390625f, 1921, 18229, false, false}, // interface/buttons/minimalscrollbarproportional
    {"minimal-scrollbar-arrow-top-down", "Interface\\Buttons\\MinimalScrollbarProportional", 17, 11, 0.8359375f, 0.96875f, 0.015625f, 0.1875f, 1921, 18228, false, false}, // interface/buttons/minimalscrollbarproportional
    {"minimal-scrollbar-arrow-bottom", "Interface\\Buttons\\MinimalScrollbarProportional", 17, 11, 0.2421875f, 0.375f, 0.8125f, 0.984375f, 1921, 18211, false, false}, // interface/buttons/minimalscrollbarproportional
    {"minimal-scrollbar-arrow-bottom-over", "Interface\\Buttons\\MinimalScrollbarProportional", 17, 11, 0.5390625f, 0.671875f, 0.015625f, 0.1875f, 1921, 18227, false, false}, // interface/buttons/minimalscrollbarproportional
    {"minimal-scrollbar-arrow-bottom-down", "Interface\\Buttons\\MinimalScrollbarProportional", 17, 11, 0.390625f, 0.5234375f, 0.015625f, 0.1875f, 1921, 18226, false, false}, // interface/buttons/minimalscrollbarproportional
    {"minimal-scrollbar-track-top", "Interface\\Buttons\\MinimalScrollbarProportional", 8, 8, 0.1640625f, 0.2265625f, 0.609375f, 0.734375f, 1921, 18216, false, false}, // interface/buttons/minimalscrollbarproportional
    {"minimal-scrollbar-track-bottom", "Interface\\Buttons\\MinimalScrollbarProportional", 8, 8, 0.0859375f, 0.1484375f, 0.765625f, 0.890625f, 1921, 18215, false, false}, // interface/buttons/minimalscrollbarproportional
    {"!minimal-scrollbar-track-middle", "Interface\\Buttons\\MinimalScrollbarVertical", 8, 1, 0.015625f, 0.140625f, 0.0f, 0.0009765625f, 1922, 18217, false, true}, // interface/buttons/minimalscrollbarvertical
    {"minimal-scrollbar-small-thumb-top", "Interface\\Buttons\\MinimalScrollbarSmallProportional", 8, 8, 0.3125f, 0.4375f, 0.84375f, 0.96875f, 2404, 23501, false, false}, // interface/buttons/minimalscrollbarsmallproportional
    {"minimal-scrollbar-small-thumb-bottom", "Interface\\Buttons\\MinimalScrollbarSmallProportional", 8, 8, 0.609375f, 0.734375f, 0.484375f, 0.609375f, 2404, 23498, false, false}, // interface/buttons/minimalscrollbarsmallproportional
    {"minimal-scrollbar-small-thumb-top-over", "Interface\\Buttons\\MinimalScrollbarSmallProportional", 8, 8, 0.46875f, 0.59375f, 0.84375f, 0.96875f, 2404, 23500, false, false}, // interface/buttons/minimalscrollbarsmallproportional
    {"minimal-scrollbar-small-thumb-bottom-over", "Interface\\Buttons\\MinimalScrollbarSmallProportional", 8, 8, 0.3125f, 0.4375f, 0.6875f, 0.8125f, 2404, 23497, false, false}, // interface/buttons/minimalscrollbarsmallproportional
    {"minimal-scrollbar-small-thumb-top-down", "Interface\\Buttons\\MinimalScrollbarSmallProportional", 8, 8, 0.46875f, 0.59375f, 0.6875f, 0.8125f, 2404, 23499, false, false}, // interface/buttons/minimalscrollbarsmallproportional
    {"minimal-scrollbar-small-thumb-bottom-down", "Interface\\Buttons\\MinimalScrollbarSmallProportional", 8, 8, 0.765625f, 0.890625f, 0.484375f, 0.609375f, 2404, 23496, false, false}, // interface/buttons/minimalscrollbarsmallproportional
    {"minimal-scrollbar-small-thumb-middle", "Interface\\Buttons\\MinimalScrollbarSmallVertical", 8, 715, 0.484375f, 0.609375f, 0.0009765625f, 0.69921875f, 2403, 23486, false, false}, // interface/buttons/minimalscrollbarsmallvertical
    {"minimal-scrollbar-small-thumb-middle-over", "Interface\\Buttons\\MinimalScrollbarSmallVertical", 8, 715, 0.328125f, 0.453125f, 0.0009765625f, 0.69921875f, 2403, 23485, false, false}, // interface/buttons/minimalscrollbarsmallvertical
    {"minimal-scrollbar-small-thumb-middle-down", "Interface\\Buttons\\MinimalScrollbarSmallVertical", 8, 715, 0.171875f, 0.296875f, 0.0009765625f, 0.69921875f, 2403, 23484, false, false}, // interface/buttons/minimalscrollbarsmallvertical
    {"common-search-clearbutton", "Interface\\Common\\CommonSearchClearButton", 20, 20, 0.03125f, 0.65625f, 0.03125f, 0.65625f, 3172, 11710, false, false}, // interface/shop/catalogshop
    // The addon list's category arrow. The 1.60.1 sheet carries it only as
    // the -2x member (20x32 on canvas 2), so the size is the 1x 10x16 the
    // list's XML draws it at. The sheet ships whole at its retail path,
    // decoded from its raw BGRA and re-encoded palettized, which keeps its
    // pixels exactly.
    {"bag-arrow", "Interface\\ContainerFrame\\BagSlots2x", 10, 16, 0.9453125f, 0.984375f, 0.0078125f, 0.2578125f, 2098, 20252, false, false}, // interface/containerframe/bagslots2x
    // The red three-slice button (ThreeSliceButtonTemplate's "128-RedButton"
    // kit: SharedButtonSmallTemplate and its siblings). Retail's sheet
    // (atlas 3556, the higher ID of the two that name these members) is
    // 512x2048 for some forty sprites; only the button's ten are cut out of
    // it, 4-pixel gaps apart, into a 512x1024 sheet, re-encoded palettized
    // from the raw BGRA (a DXT3 encoding drew a line down the end caps' edge).
    // Sizes are the member table's.
    {"128-RedButton-Right", "Interface\\Buttons\\128RedButton", 292, 128, 0.0f, 0.5703125f, 0.0f, 0.125f, 3556, 11952, false, false}, // interface/buttons/128redbutton
    {"128-RedButton-Right-Disabled", "Interface\\Buttons\\128RedButton", 292, 128, 0.0f, 0.5703125f, 0.12890625f, 0.25390625f, 3556, 11950, false, false}, // interface/buttons/128redbutton
    {"128-RedButton-Right-Pressed", "Interface\\Buttons\\128RedButton", 292, 128, 0.0f, 0.5703125f, 0.2578125f, 0.3828125f, 3556, 11951, false, false}, // interface/buttons/128redbutton
    {"128-RedButton-Left", "Interface\\Buttons\\128RedButton", 114, 128, 0.578125f, 0.80078125f, 0.0f, 0.125f, 3556, 11949, false, false}, // interface/buttons/128redbutton
    {"128-RedButton-Left-Disabled", "Interface\\Buttons\\128RedButton", 114, 128, 0.578125f, 0.80078125f, 0.12890625f, 0.25390625f, 3556, 11947, false, false}, // interface/buttons/128redbutton
    {"128-RedButton-Left-Pressed", "Interface\\Buttons\\128RedButton", 114, 128, 0.578125f, 0.80078125f, 0.2578125f, 0.3828125f, 3556, 11948, false, false}, // interface/buttons/128redbutton
    {"_128-RedButton-Center", "Interface\\Buttons\\128RedButton", 64, 128, 0.8046875f, 0.9296875f, 0.0f, 0.125f, 3556, 11955, true, false}, // interface/buttons/128redbutton
    {"_128-RedButton-Center-Disabled", "Interface\\Buttons\\128RedButton", 64, 128, 0.8046875f, 0.9296875f, 0.12890625f, 0.25390625f, 3556, 11953, true, false}, // interface/buttons/128redbutton
    {"_128-RedButton-Center-Pressed", "Interface\\Buttons\\128RedButton", 64, 128, 0.8046875f, 0.9296875f, 0.2578125f, 0.3828125f, 3556, 11954, true, false}, // interface/buttons/128redbutton
    {"128-RedButton-Highlight", "Interface\\Buttons\\128RedButton", 441, 128, 0.0f, 0.861328125f, 0.38671875f, 0.51171875f, 3556, 5451, false, false}, // interface/buttons/128redbutton
};

constexpr int kAtlasCount = sizeof(kAtlases) / sizeof(kAtlases[0]);

} // namespace Texture::AtlasData
