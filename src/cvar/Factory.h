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

// CVar::Factory — create and drive engine CVars from C++.
//
// Vanilla 1.12 only exposes CVar *reading* and *registration without a
// callback* through Lua (`RegisterCVar(name, default)`); there's no way for
// an addon to register a cvar with a change-callback or to react to value
// changes. This wraps the engine's internal registrar (`FUN_REGISTER_CVAR`)
// and value-setter (`FUN_SET_CVAR_VALUE`) so other modules can publish
// persisted, validated settings (clamped via the change callback and saved
// to Config.wtf).
//
// CVars registered here persist to `WTF\Config.wtf` (the engine forces the
// archive flag bit on at registration) and are visible to the in-game Lua
// `GetCVar`/`SetCVar` globals, since cvar storage is process-global.

namespace CVar::Factory {

// Opaque handle to an engine CVar. Stable for the cvar's lifetime (vanilla
// never reallocates a registered cvar).
using Handle = void *;

// Change callback. Fires whenever the cvar's value changes (and once at
// registration). Mirrors the engine ABI verified at FUN_SET_CVAR_VALUE:
//   - `cvar`      — this cvar's Handle
//   - `prevValue` — the value before the change (may be null/empty)
//   - `newValue`  — the value being applied
//   - `userData`  — whatever was passed to Register
// Return a nonzero value to ACCEPT the change, 0 to REJECT it (the engine
// keeps the previous value). To clamp rather than reject, set the clamped
// value with SetString from inside the callback and return 0 for the
// out-of-range input. Must tolerate being invoked with partial context.
using ChangeCallback = int(__fastcall *)(Handle cvar, const char *prevValue,
                                         const char *newValue, void *userData);

// Register a cvar (or fetch the existing one — the engine dedups by name,
// and re-registering OR-merges `flags` and updates the callback). `name`
// and `defaultValue` must outlive the process (the engine stores `name` by
// pointer; pass string literals). `defaultValue` may be null for "". The
// returned Handle is never null on success; null only if the engine
// registrar itself failed.
Handle Register(const char *name, const char *defaultValue,
                uint32_t flags = 0, ChangeCallback callback = nullptr,
                void *userData = nullptr);

// Look up an already-registered cvar by name, or null if none.
Handle Find(const char *name);

// Read the cvar's current value string (the raw engine pointer at
// `+OFF_CVAR_VALUE_STR`). Returns null if `cvar` is null. The pointer is
// valid for the cvar's lifetime.
const char *GetString(Handle cvar);

// Read the value the cvar was REGISTERED with, which the engine keeps
// alongside the live value and never rewrites. Comparing the two is how the
// engine's own `cvarlist` decides whether to print "(default: %s)", and it is
// the only way to tell "nobody has ever set this" from "set to exactly the
// default" without a marker of our own. Returns null if `cvar` is null or the
// cvar was registered without a default.
const char *GetDefaultString(Handle cvar);

// Read the value parsed as a base-10 integer, or `fallback` if `cvar` is
// null or the value isn't numeric.
int GetInt(Handle cvar, int fallback = 0);

// Set the cvar's value (fires its change callback). No-op if `cvar` is
// null. `value` is copied by the engine into the cvar's inline buffer.
void SetString(Handle cvar, const char *value);

// Convenience: set from an integer.
//
// Both setters go through the engine's own value setter, which REFUSES the
// write when the cvar carries `CVAR_FLAG_READ_ONLY` — silently, since the
// innermost writer opens with `if (flags & 0x4) return;`. Some cvars gain
// that bit partway through boot (`scriptMemory` is writable on the glue
// state and read-only once in the world), so a write that has to land needs
// to happen before the bit appears rather than fight it.
void SetInt(Handle cvar, int value);

} // namespace CVar::Factory
