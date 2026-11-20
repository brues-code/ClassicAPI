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

// `C_EncodingUtil.SerializeJSON` / `DeserializeJSON`, as the 3.80.2 client
// does them, on the library it uses: rapidjson.
//
// SerializeJSON (0x141B980D0, value writer 0x141B98340) writes with a compact
// `Writer<StringBuffer>`, so its number text (shortest round trip) and its
// escaping (`"`, `\`, control characters as \b \t \n \f \r or uppercase
// \u00XX; no UTF-8 check) are rapidjson's own.
//   - nil -> null; booleans; zero, -0 and subnormal numbers -> 0; whole
//     numbers between -2^63 and 2^63 -> integers; other numbers -> doubles.
//   - A table whose keys are exactly 1..n is an array (an empty table is
//     `[]`); any other table is an object written in `next` order.
//   - Errors, which `options.ignoreSerializationErrors` turns into null (or,
//     for a key that is not a string, skips the pair): infinity, NaN, a key
//     that is not a string, a table already on the path (a cycle), more than
//     100 nested tables, and a function, userdata or thread.
//
// DeserializeJSON (value reader 0x141B98900) is rapidjson's
// LookaheadParser example over an in-situ copy of the source, so the source
// ends at its first NUL. null -> nil (a hole in an array, no field in an
// object), the last duplicate key wins, a key ends at its first NUL. A
// source that does not start with a value, or a single number, string,
// boolean or null with more text after it, raises "attempted to deserialize
// an unknown json value"; once an array or object has opened, a later parse
// error ends the value quietly and returns what was read so far, as 3.80.2
// does (`[1,2,x` is {1, 2}).
//
// Errors are raised with the message alone. A Lua error does not run the
// destructors of the frames it leaves, so both directions unwind to the
// script function and raise there.

#include "Game.h"
#include "encoding/Args.h"

#include <rapidjson/reader.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace Encoding::JSON {

namespace {

constexpr const char *kSerializeUsage = "Usage: C_EncodingUtil.SerializeJSON(value[, options])";

// --- serialize ---------------------------------------------------------------

// The three counts 3.80.2's table test (0x1428DAF50) takes of a table.
struct KeyCounts {
    int pairs = 0;
    int integerKeys = 0; // keys that are whole numbers from 1 to 2^31 - 1
    int largest = 0;     // the largest of those
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

struct Serializer {
    void *L;
    rapidjson::Writer<rapidjson::StringBuffer> *writer;
    bool ignoreErrors;
    std::vector<const void *> path; // the tables being written, sorted
    char error[128] = {};

    bool Fail(const char *message) {
        std::snprintf(error, sizeof(error), "%s", message);
        return false;
    }

    // A value that cannot be written: false with the error, or null when
    // the options ignore errors.
    bool Unserializable(const char *message) {
        if (!ignoreErrors)
            return Fail(message);
        writer->Null();
        return true;
    }

    bool Number(double d) {
        switch (std::fpclassify(d)) {
            case FP_ZERO:
            case FP_SUBNORMAL:
                writer->Int(0);
                return true;
            case FP_INFINITE:
                return Unserializable("attempted to serialize an infinite numeric value");
            case FP_NAN:
                return Unserializable("attempted to serialize an invalid numeric value");
            default:
                if (std::floor(d) == d && d > -9223372036854775808.0 && d < 9223372036854775808.0)
                    writer->Int64(static_cast<int64_t>(d));
                else
                    writer->Double(d);
                return true;
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
        if (c.pairs == c.integerKeys && c.integerKeys == c.largest) {
            writer->StartArray();
            for (int i = 1; ok && i <= c.largest; ++i) {
                Game::Lua::RawGetI(L, idx, i);
                ok = Value(Game::Lua::GetTop(L));
                Game::Lua::SetTop(L, -2);
            }
            if (ok)
                writer->EndArray();
        } else {
            writer->StartObject();
            Game::Lua::PushNil(L);
            while (ok && Game::Lua::Next(L, idx) != 0) {
                if (Game::Lua::Type(L, -2) == Game::Lua::TYPE_STRING) {
                    writer->Key(Game::Lua::ToString(L, -2), Game::Lua::StrLen(L, -2));
                    ok = Value(Game::Lua::GetTop(L));
                } else if (!ignoreErrors) {
                    ok = Fail("attempted to serialize a table that cannot be represented as a json "
                              "array or object");
                }
                Game::Lua::SetTop(L, -2);
            }
            if (ok)
                writer->EndObject();
        }
        path.erase(std::lower_bound(path.begin(), path.end(), table));
        return ok;
    }

    bool Value(int idx) {
        const int type = Game::Lua::Type(L, idx);
        switch (type) {
            case Game::Lua::TYPE_NONE:
            case Game::Lua::TYPE_NIL:
                writer->Null();
                return true;
            case Game::Lua::TYPE_BOOLEAN:
                writer->Bool(Game::Lua::ToBoolean(L, idx) != 0);
                return true;
            case Game::Lua::TYPE_NUMBER:
                return Number(Game::Lua::ToNumber(L, idx));
            case Game::Lua::TYPE_STRING:
                writer->String(Game::Lua::ToString(L, idx), Game::Lua::StrLen(L, idx));
                return true;
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

int __fastcall Script_SerializeJSON(void *L) {
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
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        Serializer s{L, &writer, ignoreErrors};
        if (s.Value(1)) {
            Game::Lua::PushLString(L, buffer.GetString(), static_cast<unsigned int>(buffer.GetSize()));
            return 1;
        }
        std::snprintf(error, sizeof(error), "%s", s.error);
    }
    Game::Lua::Error(L, "%s", error);
    return 0;
}

// --- deserialize -------------------------------------------------------------

// rapidjson's LookaheadParser example (example/lookaheadparser) as 3.80.2
// has it; the current value is held as its type and payload rather than a
// rapidjson::Value, and GetString also gives the length.
class LookaheadParser {
public:
    explicit LookaheadParser(char *str) : ss_(str) {
        r_.IterativeParseInit();
        ParseNext();
    }

    // Handler callbacks.
    bool Null() { return Set(kHasNull, rapidjson::kNullType); }
    bool Bool(bool b) { return Set(kHasBool, b ? rapidjson::kTrueType : rapidjson::kFalseType); }
    bool Int(int i) { return SetNumber(i); }
    bool Uint(unsigned u) { return SetNumber(u); }
    bool Int64(int64_t i) { return SetNumber(static_cast<double>(i)); }
    bool Uint64(uint64_t u) { return SetNumber(static_cast<double>(u)); }
    bool Double(double d) { return SetNumber(d); }
    bool RawNumber(const char *, rapidjson::SizeType, bool) { return false; }
    bool String(const char *str, rapidjson::SizeType length, bool) {
        str_ = str;
        len_ = length;
        return Set(kHasString, rapidjson::kStringType);
    }
    bool StartObject() { st_ = kEnteringObject; return true; }
    bool Key(const char *str, rapidjson::SizeType length, bool) {
        str_ = str;
        len_ = length;
        return Set(kHasKey, rapidjson::kStringType);
    }
    bool EndObject(rapidjson::SizeType) { st_ = kExitingObject; return true; }
    bool StartArray() { st_ = kEnteringArray; return true; }
    bool EndArray(rapidjson::SizeType) { st_ = kExitingArray; return true; }

    bool EnterObject() { return Enter(kEnteringObject); }
    bool EnterArray() { return Enter(kEnteringArray); }

    const char *NextObjectKey() {
        if (st_ == kHasKey) {
            const char *result = str_;
            ParseNext();
            return result;
        }
        if (st_ != kExitingObject) {
            st_ = kError;
            return nullptr;
        }
        ParseNext();
        return nullptr;
    }

    bool NextArrayValue() {
        if (st_ == kExitingArray) {
            ParseNext();
            return false;
        }
        if (st_ == kError || st_ == kExitingObject || st_ == kHasKey) {
            st_ = kError;
            return false;
        }
        return true;
    }

    double GetDouble() {
        if (st_ != kHasNumber) {
            st_ = kError;
            return 0.;
        }
        const double result = number_;
        ParseNext();
        return result;
    }

    bool GetBool() {
        if (st_ != kHasBool) {
            st_ = kError;
            return false;
        }
        const bool result = type_ == rapidjson::kTrueType;
        ParseNext();
        return result;
    }

    void GetNull() {
        if (st_ != kHasNull) {
            st_ = kError;
            return;
        }
        ParseNext();
    }

    const char *GetString(std::size_t *length) {
        if (st_ != kHasString) {
            st_ = kError;
            return nullptr;
        }
        const char *result = str_;
        *length = len_;
        ParseNext();
        return result;
    }

    // A rapidjson::Type, or -1 for no value.
    int PeekType() const {
        if (st_ >= kHasNull && st_ <= kHasKey)
            return type_;
        if (st_ == kEnteringArray)
            return rapidjson::kArrayType;
        if (st_ == kEnteringObject)
            return rapidjson::kObjectType;
        return -1;
    }

private:
    enum State {
        kInit,
        kError,
        kHasNull,
        kHasBool,
        kHasNumber,
        kHasString,
        kHasKey,
        kEnteringObject,
        kExitingObject,
        kEnteringArray,
        kExitingArray
    };

    static constexpr unsigned kParseFlags = rapidjson::kParseDefaultFlags | rapidjson::kParseInsituFlag;

    bool Set(State st, rapidjson::Type type) {
        st_ = st;
        type_ = type;
        return true;
    }
    bool SetNumber(double d) {
        number_ = d;
        return Set(kHasNumber, rapidjson::kNumberType);
    }
    bool Enter(State expected) {
        if (st_ != expected) {
            st_ = kError;
            return false;
        }
        ParseNext();
        return true;
    }
    // 3.80.2's ParseNext (inlined in each getter, e.g. 0x142B8E050) differs
    // from the example's: any step that fails, the end of the document
    // included, puts the parser in the error state, so a root value followed
    // by more text is an error and a failed step never leaves the last value
    // to be read again.
    void ParseNext() {
        if (!r_.IterativeParseNext<kParseFlags>(ss_, *this))
            st_ = kError;
    }

    State st_ = kInit;
    rapidjson::Type type_ = rapidjson::kNullType;
    double number_ = 0.;
    const char *str_ = nullptr;
    std::size_t len_ = 0;
    rapidjson::Reader r_;
    rapidjson::InsituStringStream ss_;
};

struct Deserializer {
    void *L;
    LookaheadParser *parser;
    int depth = 0;
    const char *error = nullptr;

    // Pushes the next value; false with `error` set.
    bool Value() {
        if (depth > Args::kStackSizeLimit - 1) {
            error = "attempted to deserialize a value that is too deep (cannot exceed 100 levels)";
            return false;
        }
        if (!Game::Lua::CheckStack(L, 1)) {
            error = "stack overflow";
            return false;
        }
        switch (parser->PeekType()) {
            case rapidjson::kNullType:
                parser->GetNull();
                Game::Lua::PushNil(L);
                return true;
            case rapidjson::kFalseType:
            case rapidjson::kTrueType:
                Game::Lua::PushBool(L, parser->GetBool());
                return true;
            case rapidjson::kObjectType: {
                parser->EnterObject();
                Game::Lua::NewTable(L);
                const int table = Game::Lua::GetTop(L);
                ++depth;
                while (const char *key = parser->NextObjectKey()) {
                    if (!Value())
                        return false;
                    Game::Lua::PushString(L, key);
                    Game::Lua::Insert(L, -2);
                    Game::Lua::RawSet(L, table);
                }
                --depth;
                Game::Lua::SetTop(L, table);
                return true;
            }
            case rapidjson::kArrayType: {
                parser->EnterArray();
                Game::Lua::NewTable(L);
                const int table = Game::Lua::GetTop(L);
                ++depth;
                for (int i = 1; parser->NextArrayValue(); ++i) {
                    if (!Value())
                        return false;
                    Game::Lua::RawSetI(L, table, i);
                }
                --depth;
                Game::Lua::SetTop(L, table);
                return true;
            }
            case rapidjson::kStringType: {
                std::size_t length = 0;
                const char *str = parser->GetString(&length);
                Game::Lua::PushLString(L, str, static_cast<unsigned int>(length));
                return true;
            }
            case rapidjson::kNumberType:
                Game::Lua::PushNumber(L, parser->GetDouble());
                return true;
            default:
                error = "attempted to deserialize an unknown json value";
                return false;
        }
    }
};

int __fastcall Script_DeserializeJSON(void *L) {
    unsigned int len = 0;
    const char *src = Game::Lua::CheckLString(L, 1, &len);
    const char *error;
    {
        std::vector<char> text(src, src + len);
        text.push_back('\0');
        LookaheadParser parser(text.data());
        Deserializer d{L, &parser};
        if (d.Value())
            return 1;
        error = d.error;
    }
    Game::Lua::Error(L, "%s", error);
    return 0;
}

const Game::Doc::Field kSerializeArgs[] = {
    Game::Doc::Req("value", "LuaValueVariant",
                   "A table with keys 1 to n becomes an array, and any other table an object with "
                   "string keys. Tables nest at most 100 deep."),
    Game::Doc::Opt("options", "JSONSerializationOptions", nullptr,
                   "A table. When its ignoreSerializationErrors field is true, a value that "
                   "cannot be written becomes null, and a key that is not a string is skipped."),
};
const Game::Doc::Field kSerializeRets[] = {
    Game::Doc::Req("output", "stringView",
                   "A cycle, deeper nesting, infinity, NaN, a key that is not a string, or a "
                   "function, userdata or thread is an error unless the options ignore it."),
};
const Game::Doc::Function kSerializeJSON{"Encodes a Lua value as JSON.", kSerializeArgs, kSerializeRets};

const Game::Doc::Field kDeserializeArgs[] = {
    Game::Doc::Req("source", "stringView", "The source ends at its first NUL byte."),
};
const Game::Doc::Field kDeserializeRets[] = {
    Game::Doc::Req("value", "LuaValueVariant",
                   "null becomes nil. A source that does not start with a JSON value is an error. "
                   "An error after an array or object starts ends the value without an error."),
};
const Game::Doc::Function kDeserializeJSON{"Decodes a JSON string into a Lua value.", kDeserializeArgs,
                                           kDeserializeRets};

void Register() {
    Game::Lua::RegisterTableFunction("C_EncodingUtil", "SerializeJSON", &Script_SerializeJSON,
                                     &kSerializeJSON);
    Game::Lua::RegisterTableFunction("C_EncodingUtil", "DeserializeJSON", &Script_DeserializeJSON,
                                     &kDeserializeJSON);
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Encoding::JSON
