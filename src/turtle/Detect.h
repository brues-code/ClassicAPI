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

// Turtle WoW client detection — the gate for every module under
// `src/turtle/`. Two signals, so it works in-world AND on the glue screen:
//   * in-world: the engine-set `TURTLE_WOW_VERSION` global (e.g. `"1.18.1"`;
//     not defined in Turtle's FrameXML Lua — it's set by the client binary);
//   * glue screen: Turtle's GlueXML `TURTLE_*` string globals (GlueStrings.lua,
//     e.g. `AUTH_TURTLE_WEBSITE` / `TURTLE_ARMORY`), set at glue boot.
// Clones keep those NAMES (they rebrand only the URL/value), so name presence
// is clone-robust. No realm-list sniffing (which conflates "connected to
// Turtle" with "running the Turtle client", and varies per clone — rejected
// for the Rip combo-duration work), no binary fingerprinting.
namespace Turtle {

// True iff running on a Turtle WoW (or clone) client. LATCHES true once either
// signal is seen. Safe to call from any state / any time — including during the
// glue-time addon-registration scan, which is what lets `Addons::FlavorToc`
// pick `_Turtle.toc` consistently at both scan and in-world load. Never caches
// a false result, so an early probe (before either signal exists) can't wrongly
// pin a Turtle client as non-Turtle.
bool Detected();

// The Turtle client version string (e.g. `"1.18.1"`), or nullptr when not
// Turtle / not yet in-world (glue detection latches Detected() without a
// version — it fills once `TURTLE_WOW_VERSION` is readable in-world). Points at
// an internally-cached copy.
const char *Version();

} // namespace Turtle
