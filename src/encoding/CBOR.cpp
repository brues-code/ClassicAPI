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

#include "Game.h"

extern "C" {
#include <cbor.h>
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace Encoding::CBOR {

namespace {

constexpr int kMaxDepth = 64;

// ---------------- encode ----------------

// tinycbor's grow-and-retry contract: on buffer overflow the encoder
// switches to counting mode (end = NULL, accumulates bytes_needed) and
// encoding must CONTINUE so the final tally is complete — a nested
// container's tally only reaches the parent via close_container. So
// CborErrorOutOfMemory is not a failure here; the caller reads
// cbor_encoder_get_extra_bytes_needed() afterwards, resizes, retries.
bool EncOk(CborError err) {
    return err == CborNoError || err == CborErrorOutOfMemory;
}

bool EncodeValue(void *L, int idx, CborEncoder *enc, int depth);

bool IsLuaArray(void *L, int idx, int &outLen) {
    outLen = 0;
    int seen = 0, maxIdx = 0;
    Game::Lua::PushNil(L);
    while (Game::Lua::Next(L, idx) != 0) {
        ++seen;
        if (Game::Lua::Type(L, -2) != Game::Lua::TYPE_NUMBER) {
            Game::Lua::SetTop(L, -3);
            return false;
        }
        const double k = Game::Lua::ToNumber(L, -2);
        const int ki = static_cast<int>(k);
        if (ki < 1 || static_cast<double>(ki) != k) {
            Game::Lua::SetTop(L, -3);
            return false;
        }
        if (ki > maxIdx) maxIdx = ki;
        Game::Lua::SetTop(L, -2);
    }
    if (seen != maxIdx) return false;
    outLen = seen;
    return true;
}

int CountTablePairs(void *L, int idx) {
    int n = 0;
    Game::Lua::PushNil(L);
    while (Game::Lua::Next(L, idx) != 0) {
        ++n;
        Game::Lua::SetTop(L, -2);
    }
    return n;
}

// Encode a single Lua value at `idx` into a standalone byte vector,
// reusing the same grow-and-retry contract as Script_SerializeCBOR.
// Used to obtain a map key's encoded bytes for canonical sorting; the
// Lua stack is left untouched (EncodeValue reads but never pops).
bool EncodeValueToVector(void *L, int idx, int depth, std::vector<uint8_t> &out) {
    out.assign(64, 0);
    for (;;) {
        CborEncoder enc;
        cbor_encoder_init(&enc, out.data(), out.size(), 0);
        if (!EncodeValue(L, idx, &enc, depth))
            return false;
        const size_t extra = cbor_encoder_get_extra_bytes_needed(&enc);
        if (extra > 0) {
            out.resize(out.size() + extra);
            continue;
        }
        out.resize(cbor_encoder_get_buffer_size(&enc, out.data()));
        return true;
    }
}

bool EncodeTable(void *L, int idx, CborEncoder *enc, int depth) {
    if (depth >= kMaxDepth) return false;
    if (idx < 0) idx = Game::Lua::GetTop(L) + idx + 1;

    int arrayLen = 0;
    if (IsLuaArray(L, idx, arrayLen)) {
        CborEncoder arr;
        if (!EncOk(cbor_encoder_create_array(enc, &arr,
                                             static_cast<size_t>(arrayLen))))
            return false;
        for (int i = 1; i <= arrayLen; ++i) {
            Game::Lua::PushNumber(L, static_cast<double>(i));
            Game::Lua::RawGet(L, idx);
            if (!EncodeValue(L, -1, &arr, depth + 1)) {
                Game::Lua::SetTop(L, -2);
                return false;
            }
            Game::Lua::SetTop(L, -2);
        }
        return EncOk(cbor_encoder_close_container(enc, &arr));
    }

    const int pairs = CountTablePairs(L, idx);
    CborEncoder map;
    if (!EncOk(cbor_encoder_create_map(enc, &map, static_cast<size_t>(pairs))))
        return false;

    // Canonical CBOR (RFC 7049 §3.9): emit map entries sorted by their
    // encoded-key bytes (shorter key first, then bytewise). Lua gives no
    // stable iteration order for string-keyed tables, so without this the
    // same config serializes to different byte orders each call. We stash
    // the keys in a holding array table (`hold`), remember each key's
    // encoded bytes + slot, sort, then re-fetch key/value in sorted order.
    struct Entry {
        std::vector<uint8_t> key;
        int slot;
    };
    std::vector<Entry> entries;
    entries.reserve(static_cast<size_t>(pairs));

    Game::Lua::NewTable(L);
    const int hold = Game::Lua::GetTop(L);
    int slot = 0;
    Game::Lua::PushNil(L);
    while (Game::Lua::Next(L, idx) != 0) {
        // key @ -2, value @ -1
        std::vector<uint8_t> keyBytes;
        if (!EncodeValueToVector(L, -2, depth + 1, keyBytes)) {
            Game::Lua::SetTop(L, hold - 1); // drop hold + key + value
            return false;
        }
        ++slot;
        Game::Lua::PushNumber(L, static_cast<double>(slot));
        Game::Lua::PushValue(L, -3);        // copy key to top
        Game::Lua::RawSet(L, hold);         // hold[slot] = key
        entries.push_back(Entry{std::move(keyBytes), slot});
        Game::Lua::SetTop(L, -2);           // pop value, keep key for Next
    }

    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b) {
                  if (a.key.size() != b.key.size())
                      return a.key.size() < b.key.size();
                  return std::lexicographical_compare(
                      a.key.begin(), a.key.end(), b.key.begin(), b.key.end());
              });

    for (const Entry &e : entries) {
        Game::Lua::PushNumber(L, static_cast<double>(e.slot));
        Game::Lua::RawGet(L, hold);         // push key = hold[slot]
        if (!EncodeValue(L, -1, &map, depth + 1)) {
            Game::Lua::SetTop(L, hold - 1);
            return false;
        }
        Game::Lua::RawGet(L, idx);          // key -> value = table[key]
        if (!EncodeValue(L, -1, &map, depth + 1)) {
            Game::Lua::SetTop(L, hold - 1);
            return false;
        }
        Game::Lua::SetTop(L, -2);           // pop value
    }

    Game::Lua::SetTop(L, hold - 1);         // drop holding table
    return EncOk(cbor_encoder_close_container(enc, &map));
}

bool EncodeValue(void *L, int idx, CborEncoder *enc, int depth) {
    switch (Game::Lua::Type(L, idx)) {
        case Game::Lua::TYPE_NIL:
            return EncOk(cbor_encode_null(enc));
        case Game::Lua::TYPE_BOOLEAN:
            return EncOk(cbor_encode_boolean(enc,
                Game::Lua::ToBoolean(L, idx) != 0));
        case Game::Lua::TYPE_NUMBER: {
            const double n = Game::Lua::ToNumber(L, idx);
            // Whole numbers in [-2^53, 2^53] go out as integers so
            // `42` doesn't round-trip as `42.0`.
            if (std::isfinite(n) && n == std::floor(n) &&
                n >= -9007199254740992.0 && n <= 9007199254740992.0) {
                if (n >= 0.0)
                    return EncOk(cbor_encode_uint(enc,
                        static_cast<uint64_t>(n)));
                return EncOk(cbor_encode_negative_int(enc,
                    static_cast<uint64_t>(-n)));
            }
            return EncOk(cbor_encode_double(enc, n));
        }
        case Game::Lua::TYPE_STRING: {
            const char *s = Game::Lua::ToString(L, idx);
            const unsigned int n = Game::Lua::StrLen(L, idx);
            return EncOk(cbor_encode_text_string(enc, s, n));
        }
        case Game::Lua::TYPE_TABLE:
            return EncodeTable(L, idx, enc, depth);
        default:
            return false;
    }
}

int __fastcall Script_SerializeCBOR(void *L) {
    if (Game::Lua::GetTop(L) < 1) {
        Game::Lua::Error(L, "Usage: C_EncodingUtil.SerializeCBOR(value)");
        return 0;
    }

    // tinycbor encodes into a fixed-size buffer; on overflow it keeps
    // encoding in counting mode (EncOk treats OutOfMemory as success) so
    // the extra-bytes tally is complete, then we resize and retry once.
    std::vector<uint8_t> buf(256);
    for (;;) {
        CborEncoder enc;
        cbor_encoder_init(&enc, buf.data(), buf.size(), 0);
        if (!EncodeValue(L, 1, &enc, 0))
            return 0; // genuine "value not representable" failure
        const size_t extra = cbor_encoder_get_extra_bytes_needed(&enc);
        if (extra > 0) {
            buf.resize(buf.size() + extra);
            continue;
        }
        const size_t used = cbor_encoder_get_buffer_size(&enc, buf.data());
        Game::Lua::PushLString(L, reinterpret_cast<const char *>(buf.data()),
                               static_cast<unsigned int>(used));
        return 1;
    }
}

// ---------------- decode ----------------

bool DecodeValue(void *L, CborValue *it, int depth);

bool DecodeContainer(void *L, CborValue *it, int depth, bool isMap) {
    if (depth >= kMaxDepth) return false;
    CborValue child;
    if (cbor_value_enter_container(it, &child) != CborNoError)
        return false;

    Game::Lua::NewTable(L);
    int arrayIdx = 1;
    while (!cbor_value_at_end(&child)) {
        if (isMap) {
            // Read key.
            if (!DecodeValue(L, &child, depth + 1))
                return false;
            // Read value.
            if (!DecodeValue(L, &child, depth + 1))
                return false;
            Game::Lua::RawSet(L, -3);
        } else {
            if (!DecodeValue(L, &child, depth + 1))
                return false;
            Game::Lua::PushNumber(L, static_cast<double>(arrayIdx++));
            Game::Lua::Insert(L, -2);
            Game::Lua::RawSet(L, -3);
        }
    }
    return cbor_value_leave_container(it, &child) == CborNoError;
}

bool DecodeValue(void *L, CborValue *it, int depth) {
    const CborType t = cbor_value_get_type(it);
    switch (t) {
        case CborIntegerType: {
            // Unsigned or negative — tinycbor distinguishes via the
            // raw value, but `get_int64_checked` clamps and signals
            // overflow. For our Lua-number target we want a double
            // anyway, so go via uint64/raw and convert.
            uint64_t raw;
            if (cbor_value_get_raw_integer(it, &raw) != CborNoError)
                return false;
            double n;
            if (cbor_value_is_negative_integer(it))
                n = -1.0 - static_cast<double>(raw);
            else
                n = static_cast<double>(raw);
            Game::Lua::PushNumber(L, n);
            break;
        }
        case CborBooleanType: {
            bool b;
            if (cbor_value_get_boolean(it, &b) != CborNoError)
                return false;
            Game::Lua::PushBool(L, b);
            break;
        }
        case CborNullType:
        case CborUndefinedType:
            Game::Lua::PushNil(L);
            break;
        case CborFloatType: {
            float f;
            if (cbor_value_get_float(it, &f) != CborNoError)
                return false;
            Game::Lua::PushNumber(L, static_cast<double>(f));
            break;
        }
        case CborDoubleType: {
            double d;
            if (cbor_value_get_double(it, &d) != CborNoError)
                return false;
            Game::Lua::PushNumber(L, d);
            break;
        }
        case CborHalfFloatType: {
            float f;
            if (cbor_value_get_half_float_as_float(it, &f) != CborNoError)
                return false;
            Game::Lua::PushNumber(L, static_cast<double>(f));
            break;
        }
        case CborTextStringType:
        case CborByteStringType: {
            char *s = nullptr;
            size_t n = 0;
            CborError err = (t == CborTextStringType)
                ? cbor_value_dup_text_string(it, &s, &n, it)
                : cbor_value_dup_byte_string(it, reinterpret_cast<uint8_t **>(&s),
                                             &n, it);
            if (err != CborNoError) {
                std::free(s);
                return false;
            }
            Game::Lua::PushLString(L, s, static_cast<unsigned int>(n));
            std::free(s);
            // dup_*_string advances `it` past the string, so skip the
            // bottom-of-function advance.
            return true;
        }
        case CborArrayType:
            return DecodeContainer(L, it, depth, /*isMap=*/false);
        case CborMapType:
            return DecodeContainer(L, it, depth, /*isMap=*/true);
        case CborTagType: {
            // Skip the tag, decode the contained value.
            if (cbor_value_advance_fixed(it) != CborNoError)
                return false;
            return DecodeValue(L, it, depth);
        }
        default:
            return false;
    }
    return cbor_value_advance_fixed(it) == CborNoError;
}

int __fastcall Script_DeserializeCBOR(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: C_EncodingUtil.DeserializeCBOR(data)");
        return 0;
    }
    const char *src = Game::Lua::ToString(L, 1);
    const unsigned int srcLen = Game::Lua::StrLen(L, 1);
    if (src == nullptr)
        return 0;

    CborParser parser;
    CborValue it;
    if (cbor_parser_init(reinterpret_cast<const uint8_t *>(src), srcLen, 0,
                         &parser, &it) != CborNoError)
        return 0;
    if (!DecodeValue(L, &it, 0))
        return 0;
    return 1;
}

const Game::Doc::Field kSerializeArgs[] = {
    Game::Doc::Opt("value", "any", nullptr,
                   "Nil, a boolean, number or string, or a table of these; map keys are "
                   "written in sorted order."),
};
const Game::Doc::Field kSerializeRets[] = {
    Game::Doc::Opt("output", "string", nullptr,
                   "Nothing when the value holds a function or userdata, or tables nested "
                   "64 deep."),
};
const Game::Doc::Function kSerializeCBOR{"Encodes a Lua value as CBOR.", kSerializeArgs,
                                         kSerializeRets};

const Game::Doc::Field kDeserializeArgs[] = {
    Game::Doc::Req("source", "string"),
};
const Game::Doc::Field kDeserializeRets[] = {
    Game::Doc::Opt("value", "any", nullptr, "Nothing when the source is not valid CBOR."),
};
const Game::Doc::Function kDeserializeCBOR{"Decodes a CBOR string into a Lua value.",
                                           kDeserializeArgs, kDeserializeRets};

void Register() {
    Game::Lua::RegisterTableFunction("C_EncodingUtil", "SerializeCBOR",
                                     &Script_SerializeCBOR, &kSerializeCBOR);
    Game::Lua::RegisterTableFunction("C_EncodingUtil", "DeserializeCBOR",
                                     &Script_DeserializeCBOR, &kDeserializeCBOR);
}

} // namespace

static const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace Encoding::CBOR
