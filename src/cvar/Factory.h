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
// changes. This wraps the engine's own registrar (`FUN_REGISTER_CVAR`) and
// value-setter (`FUN_SET_CVAR_VALUE`) so modules can publish cvars the way the
// engine publishes its own: with a help line and a console category, a
// validating callback, and persistence to `WTF\Config.wtf` (the registrar
// forces the archive flag bit on). Cvar storage is process-global, so the
// in-game and glue Lua `GetCVar`/`SetCVar` both see them.

namespace CVar::Factory {

// Opaque handle to an engine CVar. Stable for the cvar's lifetime (vanilla
// never reallocates a registered cvar).
using Handle = void *;

// Change callback. Mirrors the engine ABI verified at FUN_SET_CVAR_VALUE:
//   - `cvar`      — this cvar's Handle
//   - `prevValue` — the value before the change (may be null/empty)
//   - `newValue`  — the value being applied
//   - `userData`  — whatever was passed to Register
// Return a nonzero value to ACCEPT the change, 0 to REJECT it (the engine
// keeps the previous value). To clamp rather than reject, set the clamped
// value with SetString from inside the callback and return 0 for the
// out-of-range input.
//
// Registration calls it too, and the result means different things there:
//   - a NEW cvar runs its default through it, and a rejection leaves the
//     value EMPTY rather than falling back to anything — so a callback must
//     accept its own default;
//   - an EXISTING one (every /reload, and any cvar Config.wtf created) is
//     called with prev == new == the current value, and the result is
//     ignored.
using ChangeCallback = int(__fastcall *)(Handle cvar, const char *prevValue,
                                         const char *newValue, void *userData);

// Register a cvar, or fetch the existing one (the engine dedups by name and
// replaces the callback). `name`, `help` and `defaultValue` must be string
// literals: the engine keeps some of them by pointer. `help` is the console's
// help line (copied into a 0x80-byte buffer, so at most 127 characters; null
// for none) and `category` one of `Offsets::CVAR_CATEGORY_*`. For a cvar that
// also exists in a newer client, pass that client's exact help and category.
//
// Help and category take effect only when this call creates the cvar. A cvar
// with a line in Config.wtf already exists by then (the file loads before any
// registration) and keeps the console `set` command's category and no help,
// which is how the engine's own cvars behave too.
//
// The returned Handle is null only if the engine registrar failed.
Handle Register(const char *name, const char *help, int category,
                const char *defaultValue, ChangeCallback callback = nullptr,
                void *userData = nullptr);

// Look up an already-registered cvar by name, or null if none. This is the
// lookup every Lua cvar function uses, so it finds only cvars Lua can see.
Handle Find(const char *name);

// Read the cvar's current value string (the raw engine pointer at
// `+OFF_CVAR_VALUE_STR`). Returns null if `cvar` is null. The pointer is
// valid until the next set.
const char *GetString(Handle cvar);

// Read the value the cvar was REGISTERED with, which the engine keeps
// alongside the live value and never rewrites. Comparing the two is how the
// engine's own `cvarlist` decides whether to print "(default: %s)", and it is
// the only way to tell "nobody has ever set this" from "set to exactly the
// default" without a marker of our own. Returns null if `cvar` is null or the
// cvar was registered without a default.
const char *GetDefaultString(Handle cvar);

// The value as an integer / float — the forms the engine's setter parses and
// stores beside the string on every set (SStrToInt / SStrToFloat), which is
// what the engine's own readers use. `fallback` is returned only for a null
// `cvar`; an empty or non-numeric value reads as 0, as it does for the engine.
int GetInt(Handle cvar, int fallback = 0);
float GetFloat(Handle cvar, float fallback = 0.0f);

// True when the cvar carries `CVAR_FLAG_READ_ONLY`. A set on such a cvar is
// silently dropped by the engine (the inner setter opens with `if (flags &
// 0x4) return;`). Only `realmList`, `realmName` and `scriptMemory` ever get
// the bit: `CGGameUI::InitializeGame` sets it on entering the world and
// `ShutdownGame` clears it (see `FUN_CVAR_SET_READ_ONLY`), so a write to one
// of them has to happen on the glue screens. False for a null `cvar`.
bool IsReadOnly(Handle cvar);

// Set the cvar's value (fires its change callback). No-op if `cvar` is
// null. `value` is copied by the engine.
//
// `persist` is the setter's `force` argument, the only thing that marks the
// config dirty (FUN_0063E0B0 sets VAR_CVAR_CONFIG_DIRTY only when it is
// nonzero). False applies the value for this session without asking the
// config writer to run; `CVar::Temp` relies on that.
void SetString(Handle cvar, const char *value, bool persist = true);

// Convenience: set from an integer.
void SetInt(Handle cvar, int value);

} // namespace CVar::Factory
