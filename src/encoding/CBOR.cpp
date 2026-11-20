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

// `C_EncodingUtil.SerializeCBOR` / `DeserializeCBOR`, as the 3.80.2 client
// does them with its own CBOR code.
//
// SerializeCBOR (0x141B98C40, value writer 0x141B98F00):
//   - nil F6, false F4, true F5; strings are byte strings.
//   - Zero, -0 and subnormal numbers are 0; whole numbers between -2^63 and
//     2^63 are integers in their shortest head; other numbers take the
//     shortest float that holds them exactly (half, single, double), and
//     infinity and NaN are the half floats 7C00 / FC00 / 7E00.
//   - A table whose keys are all whole numbers from 1 to 2^31 - 1, with the
//     largest at most twice the number of keys, is an array as long as the
//     largest key, its holes null; any other table is a map in `next` order,
//     its keys written as values.
//   - Errors, which `options.ignoreSerializationErrors` turns into undefined
//     (F7): a cycle, more than 100 nested tables, and a function, userdata or
//     thread. The messages are JSON's.
//
// DeserializeCBOR (0x140C1E160, item reader 0x141B997A0, head reader
// 0x141BC98E0) reads items until the source ends and returns them all, at
// most 100 values on the stack. Its head reader takes additional info 28 to
// 30 as 0 and 31 as an indefinite length, and a one-byte simple value (F8 nn)
// is read as the simple value nn. Integers are signed 64-bit (a uint above
// 2^63 reads as negative); half, single and double floats; null and
// undefined are nil; text and byte strings are strings; maps use the last
// duplicate key. Tags, indefinite lengths and simple values other than
// false, true, null and undefined raise errors. The reader holds no C++
// state, so its errors are raised where they happen.

#include "Game.h"
#include "encoding/Args.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Encoding::CBOR {

namespace {

constexpr const char *kSerializeUsage = "Usage: C_EncodingUtil.SerializeCBOR(value[, options])";

// --- serialize ---------------------------------------------------------------

struct KeyCounts {
    int pairs = 0;
    int integerKeys = 0; // keys that are whole numbers from 1 to 2^31 - 1
    int largest = 0;
};

KeyCounts CountKeys(void *L, int idx) {
    KeyCounts c;
    Game::Lua::PushNil(L);
    while (Game::Lua::Next(L, idx) != 0) {
        ++c.pairs;
        if (Game::Lua::Type(L, -2) == Game::Lua::TYPE_NUMBER) {
            const double k = Game::Lua::ToNumber(L, -2);
            if (k >= 1.0 && k <= 2147483647.0 && std::floor(k) == k) {
                ++c.integerKeys;
                c.largest = std::max(c.largest, static_cast<int>(k));
            }
        }
        Game::Lua::SetTop(L, -2);
    }
    return c;
}

uint32_t FloatBits(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

// The half float equal to `f`, when there is one.
bool HalfOf(float f, uint16_t *half) {
    const uint32_t bits = FloatBits(f);
    const uint16_t sign = static_cast<uint16_t>((bits >> 16) & 0x8000);
    const int exp = static_cast<int>((bits >> 23) & 0xFF) - 127;
    const uint32_t mant = bits & 0x7FFFFF;
    if (exp >= -14 && exp <= 15) {
        if ((mant & 0x1FFF) != 0)
            return false;
        *half = static_cast<uint16_t>(sign | ((exp + 15) << 10) | (mant >> 13));
        return true;
    }
    if (exp >= -24 && exp < -14) {
        const uint32_t full = mant | 0x800000;
        const int shift = -1 - exp; // the value is full * 2^(exp - 23); a half subnormal is m * 2^-24
        if ((full & ((1u << shift) - 1)) != 0)
            return false;
        *half = static_cast<uint16_t>(sign | (full >> shift));
        return true;
    }
    return false;
}

struct Serializer {
    void *L;
    bool ignoreErrors;
    std::string out;
    std::vector<const void *> path; // the tables being written, sorted
    char error[128] = {};

    void Byte(uint8_t b) { out += static_cast<char>(b); }

    void BigEndian(uint64_t v, int bytes) {
        for (int i = bytes - 1; i >= 0; --i)
            Byte(static_cast<uint8_t>(v >> (i * 8)));
    }

    void Head(uint8_t major, uint64_t v) {
        const uint8_t m = static_cast<uint8_t>(major << 5);
        if (v < 24) {
            Byte(static_cast<uint8_t>(m | v));
        } else if (v <= 0xFF) {
            Byte(m | 24);
            BigEndian(v, 1);
        } else if (v <= 0xFFFF) {
            Byte(m | 25);
            BigEndian(v, 2);
        } else if (v <= 0xFFFFFFFFull) {
            Byte(m | 26);
            BigEndian(v, 4);
        } else {
            Byte(m | 27);
            BigEndian(v, 8);
        }
    }

    bool Fail(const char *message) {
        std::snprintf(error, sizeof(error), "%s", message);
        return false;
    }

    bool Unserializable(const char *message) {
        if (!ignoreErrors)
            return Fail(message);
        Byte(0xF7);
        return true;
    }

    void Float(double d) {
        if (std::isnan(d)) {
            Byte(0xF9);
            BigEndian(0x7E00, 2);
            return;
        }
        if (std::isinf(d)) {
            Byte(0xF9);
            BigEndian(d > 0 ? 0x7C00 : 0xFC00, 2);
            return;
        }
        const float f = static_cast<float>(d);
        if (static_cast<double>(f) == d) {
            uint16_t half;
            if (HalfOf(f, &half)) {
                Byte(0xF9);
                BigEndian(half, 2);
            } else {
                Byte(0xFA);
                BigEndian(FloatBits(f), 4);
            }
            return;
        }
        uint64_t bits;
        std::memcpy(&bits, &d, sizeof(bits));
        Byte(0xFB);
        BigEndian(bits, 8);
    }

    void Number(double d) {
        const int cls = std::fpclassify(d);
        if (cls == FP_ZERO || cls == FP_SUBNORMAL) {
            Byte(0x00);
        } else if (cls == FP_NORMAL && std::floor(d) == d && d > -9223372036854775808.0 &&
                   d < 9223372036854775808.0) {
            const int64_t i = static_cast<int64_t>(d);
            if (i >= 0)
                Head(0, static_cast<uint64_t>(i));
            else
                Head(1, static_cast<uint64_t>(~i));
        } else {
            Float(d);
        }
    }

    bool Table(int idx) {
        if (!Game::Lua::CheckStack(L, 3))
            return Fail("stack overflow");
        const void *table = Game::Lua::ToPointer(L, idx);
        const auto at = std::lower_bound(path.begin(), path.end(), table);
        if (at != path.end() && *at == table)
            return Unserializable("attempted to serialize a recursive table structure");
        if (path.size() >= static_cast<std::size_t>(Args::kStackSizeLimit))
            return Unserializable(
                "attempted to serialize a table structure that is too deep (cannot exceed 100 levels)");
        path.insert(at, table);

        const KeyCounts c = CountKeys(L, idx);
        bool ok = true;
        if (c.pairs == c.integerKeys && c.largest * 0.5 <= c.pairs) {
            Head(4, static_cast<uint64_t>(c.largest));
            for (int i = 1; ok && i <= c.largest; ++i) {
                Game::Lua::RawGetI(L, idx, i);
                ok = Value(Game::Lua::GetTop(L));
                Game::Lua::SetTop(L, -2);
            }
        } else {
            Head(5, static_cast<uint64_t>(c.pairs));
            Game::Lua::PushNil(L);
            while (ok && Game::Lua::Next(L, idx) != 0) {
                const int top = Game::Lua::GetTop(L);
                ok = Value(top - 1) && Value(top);
                Game::Lua::SetTop(L, -2);
            }
        }
        path.erase(std::lower_bound(path.begin(), path.end(), table));
        return ok;
    }

    bool Value(int idx) {
        const int type = Game::Lua::Type(L, idx);
        switch (type) {
            case Game::Lua::TYPE_NONE:
            case Game::Lua::TYPE_NIL:
                Byte(0xF6);
                return true;
            case Game::Lua::TYPE_BOOLEAN:
                Byte(Game::Lua::ToBoolean(L, idx) != 0 ? 0xF5 : 0xF4);
                return true;
            case Game::Lua::TYPE_NUMBER:
                Number(Game::Lua::ToNumber(L, idx));
                return true;
            case Game::Lua::TYPE_STRING: {
                const unsigned int len = Game::Lua::StrLen(L, idx);
                Head(2, len);
                out.append(Game::Lua::ToString(L, idx), len);
                return true;
            }
            case Game::Lua::TYPE_TABLE:
                return Table(idx);
            default: {
                char message[64];
                std::snprintf(message, sizeof(message), "attempted to serialize a %s value",
                              Game::Lua::TypeName(L, type));
                return Unserializable(message);
            }
        }
    }
};

int __fastcall Script_SerializeCBOR(void *L) {
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_NONE) {
        Game::Lua::ArgError(L, 1, "value expected");
        return 0;
    }
    Game::Lua::SetTop(L, 2);
    bool ignoreErrors = false;
    if (Game::Lua::Type(L, 2) == Game::Lua::TYPE_TABLE) {
        Game::Lua::PushString(L, "ignoreSerializationErrors");
        Game::Lua::GetTable(L, 2);
        ignoreErrors = Game::Lua::ToBoolean(L, -1) != 0;
        Game::Lua::SetTop(L, 2);
    } else if (Game::Lua::Type(L, 2) != Game::Lua::TYPE_NIL) {
        Game::Lua::Error(L, "%s", kSerializeUsage);
        return 0;
    }

    char error[sizeof(Serializer::error)];
    {
        Serializer s{L, ignoreErrors};
        if (s.Value(1)) {
            Game::Lua::PushLString(L, s.out.data(), static_cast<unsigned int>(s.out.size()));
            return 1;
        }
        std::snprintf(error, sizeof(error), "%s", s.error);
    }
    Game::Lua::Error(L, "%s", error);
    return 0;
}

// --- deserialize -------------------------------------------------------------

// The item kinds 3.80.2's head reader sets.
enum Kind {
    KIND_UNKNOWN = 0,
    KIND_UNDEFINED = 1,
    KIND_NULL = 2,
    KIND_BOOL = 3,
    KIND_INT = 4,
    KIND_FLOAT = 5,
    KIND_TEXT = 6,
    KIND_BYTES = 7,
    KIND_ARRAY = 8,
    KIND_MAP = 9,
    KIND_UNSUPPORTED = 10,
};

constexpr uint64_t kIndefinite = ~0ull;

struct Item {
    Kind kind;
    uint64_t value; // the head's argument: a length, count, integer or float bits
    bool flag;
    double number;
};

float HalfToFloat(uint16_t h) {
    const uint32_t sign = static_cast<uint32_t>(h & 0x8000) << 16;
    const int exp = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x3FF;
    uint32_t bits;
    if (exp == 0x1F) {
        bits = sign | 0x7F800000 | (mant << 13);
    } else if (exp != 0) {
        bits = sign | static_cast<uint32_t>(exp - 15 + 127) << 23 | (mant << 13);
    } else if (mant == 0) {
        bits = sign;
    } else {
        int e = -14;
        while ((mant & 0x400) == 0) {
            mant <<= 1;
            --e;
        }
        bits = sign | static_cast<uint32_t>(e + 127) << 23 | ((mant & 0x3FF) << 13);
    }
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

struct Decoder {
    void *L;
    const uint8_t *p;
    const uint8_t *end;
    int depth = 0;

    bool Bytes(std::size_t n, uint64_t *out) {
        if (static_cast<std::size_t>(end - p) < n)
            return false;
        uint64_t v = 0;
        for (std::size_t i = 0; i < n; ++i)
            v = (v << 8) | p[i];
        p += n;
        *out = v;
        return true;
    }

    bool Head(Item *item) {
        uint64_t first;
        if (!Bytes(1, &first))
            return false;
        const int major = static_cast<int>(first >> 5);
        const int info = static_cast<int>(first & 0x1F);
        uint64_t v;
        if (info < 24)
            v = static_cast<uint64_t>(info);
        else if (info <= 27) {
            if (!Bytes(static_cast<std::size_t>(1) << (info - 24), &v))
                return false;
        } else
            v = info == 31 ? kIndefinite : 0;
        item->value = v;
        switch (major) {
            case 0:
                item->kind = KIND_INT;
                item->number = static_cast<double>(static_cast<int64_t>(v));
                break;
            case 1:
                item->kind = KIND_INT;
                item->number = static_cast<double>(~static_cast<int64_t>(v));
                break;
            case 2:
                item->kind = KIND_BYTES;
                break;
            case 3:
                item->kind = KIND_TEXT;
                break;
            case 4:
                item->kind = KIND_ARRAY;
                break;
            case 5:
                item->kind = KIND_MAP;
                break;
            case 6:
                item->kind = KIND_UNKNOWN; // a tag
                break;
            default: {
                const uint64_t simple = info == 24 ? v : static_cast<uint64_t>(info);
                switch (simple) {
                    case 20:
                    case 21:
                        item->kind = KIND_BOOL;
                        item->flag = simple == 21;
                        break;
                    case 22:
                        item->kind = KIND_NULL;
                        break;
                    case 23:
                        item->kind = KIND_UNDEFINED;
                        break;
                    case 25:
                        item->kind = KIND_FLOAT;
                        item->number = HalfToFloat(static_cast<uint16_t>(v));
                        break;
                    case 26: {
                        const uint32_t bits = static_cast<uint32_t>(v);
                        float f;
                        std::memcpy(&f, &bits, sizeof(f));
                        item->kind = KIND_FLOAT;
                        item->number = f;
                        break;
                    }
                    case 27:
                        item->kind = KIND_FLOAT;
                        std::memcpy(&item->number, &v, sizeof(v));
                        break;
                    case 31:
                        item->kind = KIND_UNSUPPORTED;
                        break;
                    default:
                        item->kind = KIND_UNKNOWN;
                        break;
                }
            }
        }
        return true;
    }

    void Raise(const char *message) { Game::Lua::Error(L, "%s", message); }

    // A length or count that must be definite and fit an int.
    int Count(const Item &item, const char *indefinite, const char *tooLarge) {
        if (item.value == kIndefinite)
            Raise(indefinite);
        if (item.value > 0x7FFFFFFF)
            Raise(tooLarge);
        return static_cast<int>(item.value);
    }

    void Value() {
        if (depth > Args::kStackSizeLimit - 1)
            Raise("attempted to deserialize a value that is too deep (cannot exceed 100 levels)");
        if (!Game::Lua::CheckStack(L, 1))
            Raise("stack overflow");
        Item item{};
        if (!Head(&item))
            Raise("attempted to deserialize an incomplete or ill-formed data item");
        switch (item.kind) {
            case KIND_UNDEFINED:
            case KIND_NULL:
                Game::Lua::PushNil(L);
                break;
            case KIND_BOOL:
                Game::Lua::PushBool(L, item.flag);
                break;
            case KIND_INT:
            case KIND_FLOAT:
                Game::Lua::PushNumber(L, item.number);
                break;
            case KIND_TEXT:
            case KIND_BYTES: {
                const int len = Count(item, "attempted to deserialize a string that has an indefinite length",
                                      "attempted to deserialize a string that is too large (cannot exceed "
                                      "2147483647 bytes)");
                if (end - p < len)
                    Raise("attempted to deserialize an incomplete string");
                Game::Lua::PushLString(L, reinterpret_cast<const char *>(p), static_cast<unsigned int>(len));
                p += len;
                break;
            }
            case KIND_ARRAY: {
                const int n = Count(item, "attempted to deserialize an array that has an indefinite length",
                                    "attempted to deserialize an array that is too large (cannot exceed "
                                    "2147483647 elements)");
                Game::Lua::NewTable(L);
                const int table = Game::Lua::GetTop(L);
                ++depth;
                for (int i = 1; i <= n; ++i) {
                    Value();
                    Game::Lua::RawSetI(L, table, i);
                }
                --depth;
                break;
            }
            case KIND_MAP: {
                const int n = Count(item, "attempted to deserialize a map that has an indefinite length",
                                    "attempted to deserialize a map that is too large (cannot exceed "
                                    "2147483647 elements)");
                Game::Lua::NewTable(L);
                const int table = Game::Lua::GetTop(L);
                ++depth;
                for (int i = 0; i < n; ++i) {
                    Value();
                    Value();
                    Game::Lua::RawSet(L, table);
                }
                --depth;
                break;
            }
            case KIND_UNSUPPORTED:
                Raise("attempted to deserialize an unsupported cbor value");
                break;
            default:
                Raise("attempted to deserialize an unknown cbor value");
                break;
        }
    }
};

int __fastcall Script_DeserializeCBOR(void *L) {
    unsigned int len = 0;
    const auto *src = reinterpret_cast<const uint8_t *>(Game::Lua::CheckLString(L, 1, &len));
    Decoder d{L, src, src + len};
    while (d.p < d.end) {
        if (Game::Lua::GetTop(L) > Args::kStackSizeLimit)
            d.Raise("attempted to deserialize too many values (cannot exceed 100 values)");
        d.Value();
    }
    return Game::Lua::GetTop(L) - 1;
}

const Game::Doc::Field kSerializeArgs[] = {
    Game::Doc::Req("value", "LuaValueVariant",
                   "A table with keys 1 to n, where at least half of the keys are present, becomes "
                   "an array, and any other table a map. Tables nest at most 100 deep."),
    Game::Doc::Opt("options", "CBORSerializationOptions", nullptr,
                   "A table. When its ignoreSerializationErrors field is true, a value that "
                   "cannot be written becomes undefined."),
};
const Game::Doc::Field kSerializeRets[] = {
    Game::Doc::Req("output", "stringView",
                   "A cycle, deeper nesting, or a function, userdata or thread is an error unless "
                   "the options ignore it."),
};
const Game::Doc::Function kSerializeCBOR{"Encodes a Lua value as CBOR.", kSerializeArgs, kSerializeRets};

const Game::Doc::Field kDeserializeArgs[] = {
    Game::Doc::Req("source", "stringView"),
};
const Game::Doc::Field kDeserializeRets[] = {
    Game::Doc::Opt("...", "LuaValueVariant", nullptr,
                   "One value for each item in the source, at most 100. Tags, indefinite lengths "
                   "and data that is not complete are errors."),
};
const Game::Doc::Function kDeserializeCBOR{"Decodes a CBOR string into Lua values.", kDeserializeArgs,
                                           kDeserializeRets};

void Register() {
    Game::Lua::RegisterTableFunction("C_EncodingUtil", "SerializeCBOR", &Script_SerializeCBOR,
                                     &kSerializeCBOR);
    Game::Lua::RegisterTableFunction("C_EncodingUtil", "DeserializeCBOR", &Script_DeserializeCBOR,
                                     &kDeserializeCBOR);
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Encoding::CBOR
