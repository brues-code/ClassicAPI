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

// `<Scripts>` handlers with `inherit="prepend"` / `"append"` — see
// ScriptInherit.cpp. Called from the frame loader's observer
// (Xml::ParentKey), around its `function=` / `method=` binding.

namespace Xml::ScriptInherit {

// True when a handler element named `script` under the <Scripts> node
// `scripts` carries `inherit="prepend"` or `"append"`: that script's
// handler is set by Apply, so the `function=` binding leaves it alone.
bool Chained(const void *scripts, const char *script);

// Joins `node`'s chained handlers with the ones its templates set. Run
// after the engine has loaded `node`, before the `function=` binding.
void Apply(void *object, const void *node);

// Records `object`'s current handlers for the enclosing node that chains
// onto them. Run after the `function=` binding.
void Capture(void *object);

} // namespace Xml::ScriptInherit
