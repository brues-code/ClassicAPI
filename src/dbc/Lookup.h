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

// `DBC::*` — shared readers for the engine's `.dbc` table cache.
//
// Every DBC instance the engine loads has the same 5-DWORD class shape
// in `.data`: records-pointer at `+0x08`, count at `+0x0C` (see
// `docs/DBCs.md`). Lookup boilerplate is the same across every table:
//
//   1. read count from `countVar`
//   2. bounds-check `id` against `[1, count]` (records are 1-based;
//      `records[0]` is unused per the engine's storage convention)
//   3. deref records-pointer from `recordsVar`
//   4. index records[id]
//   5. null-guard the slot
//   6. read the field
//
// This module folds that chain into one of two readers depending on
// whether the field is a single string pointer or a 9-entry locale
// array (one ptr per supported locale, indexed by
// `VAR_LOCALE_INDEX`). Both return `nullptr` for OOR ids, empty
// slots, or missing/empty field values — callers compare against
// `nullptr` rather than checking individual error conditions.
namespace DBC {

// Resolves `id` to its record pointer in the table whose records-base
// global is at `recordsVar` and count global at `countVar`. Returns
// `nullptr` for `id == 0`, OOR ids (> count, which includes -1 and every
// other negative id), and unpopulated slots.
// Caller reads whatever fields it needs off the returned record.
//
// Use this when `LocalizedField` / `StringField` don't fit — e.g. for
// integer or float fields, for fields that point into a *secondary*
// DBC that needs its own chained lookup, or when iterating the whole
// table.
const uint8_t *Record(uintptr_t recordsVar, uintptr_t countVar, uint32_t id);

// Reads a localized string field from a DBC record. The field at
// `offset` is treated as a 9-entry array of `const char *`, indexed
// by the engine's current locale at `VAR_LOCALE_INDEX`.
//
// Used for the `Name`/`Description` etc. fields on `Spell.dbc`,
// `ChrRaces.dbc`, `AreaTable.dbc`, and every other table whose
// schema marks the field as `Localized`.
const char *LocalizedField(uintptr_t recordsVar, uintptr_t countVar,
                           uint32_t id, int offset);

// Reads a single-string field — the field at `offset` is a `const
// char *`, no locale array. Used for filename / english-name fields
// like `ChrRaces.dbc::ClientFilename` or
// `ChrClasses.dbc::ClassFile`.
const char *StringField(uintptr_t recordsVar, uintptr_t countVar,
                        uint32_t id, int offset);

} // namespace DBC
