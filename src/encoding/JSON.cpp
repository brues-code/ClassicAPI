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

#define PICOJSON_USE_INT64
#include <picojson/picojson.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace Encoding::JSON {

namespace {

constexpr int kMaxDepth = 64;

bool LuaToPicojson(void *L, int idx, picojson::value &out, int depth);

// Consecutive 1..N integer keys → JSON array; anything else → object.
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

bool LuaTableToPicojson(void *L, int idx, picojson::value &out, int depth) {
    if (depth >= kMaxDepth) return false;
    if (idx < 0) idx = Game::Lua::GetTop(L) + idx + 1;

    int arrayLen = 0;
    if (IsLuaArray(L, idx, arrayLen)) {
        picojson::array arr;
        arr.reserve(arrayLen);
        for (int i = 1; i <= arrayLen; ++i) {
            Game::Lua::PushNumber(L, static_cast<double>(i));
            Game::Lua::RawGet(L, idx);
            picojson::value child;
            if (!LuaToPicojson(L, -1, child, depth + 1)) {
                Game::Lua::SetTop(L, -2);
                return false;
            }
            arr.push_back(std::move(child));
            Game::Lua::SetTop(L, -2);
        }
        out = picojson::value(arr);
        return true;
    }

    picojson::object obj;
    Game::Lua::PushNil(L);
    while (Game::Lua::Next(L, idx) != 0) {
        const int keyType = Game::Lua::Type(L, -2);
        std::string key;
        if (keyType == Game::Lua::TYPE_STRING) {
            const char *kstr = Game::Lua::ToString(L, -2);
            const unsigned int klen = Game::Lua::StrLen(L, -2);
            key.assign(kstr, klen);
        } else if (keyType == Game::Lua::TYPE_NUMBER) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.14g",
                          Game::Lua::ToNumber(L, -2));
            key = buf;
        } else {
            // Drop unsupported key types.
            Game::Lua::SetTop(L, -2);
            continue;
        }

        picojson::value child;
        if (!LuaToPicojson(L, -1, child, depth + 1)) {
            Game::Lua::SetTop(L, -3);
            return false;
        }
        obj[key] = std::move(child);
        Game::Lua::SetTop(L, -2);
    }
    out = picojson::value(obj);
    return true;
}

bool LuaToPicojson(void *L, int idx, picojson::value &out, int depth) {
    switch (Game::Lua::Type(L, idx)) {
        case Game::Lua::TYPE_NIL:
            out = picojson::value();
            return true;
        case Game::Lua::TYPE_BOOLEAN:
            out = picojson::value(Game::Lua::ToBoolean(L, idx) != 0);
            return true;
        case Game::Lua::TYPE_NUMBER: {
            const double n = Game::Lua::ToNumber(L, idx);
            if (std::isnan(n) || std::isinf(n)) {
                out = picojson::value(); // JSON has no nan/inf → null
                return true;
            }
            out = picojson::value(n);
            return true;
        }
        case Game::Lua::TYPE_STRING: {
            const char *s = Game::Lua::ToString(L, idx);
            const unsigned int n = Game::Lua::StrLen(L, idx);
            out = picojson::value(std::string(s, n));
            return true;
        }
        case Game::Lua::TYPE_TABLE:
            return LuaTableToPicojson(L, idx, out, depth);
        default:
            return false;
    }
}

void PicojsonToLua(void *L, const picojson::value &v) {
    if (v.is<picojson::null>()) {
        Game::Lua::PushNil(L);
    } else if (v.is<bool>()) {
        Game::Lua::PushBool(L, v.get<bool>());
    } else if (v.is<double>()) {
        Game::Lua::PushNumber(L, v.get<double>());
    } else if (v.is<int64_t>()) {
        Game::Lua::PushNumber(L,
            static_cast<double>(v.get<int64_t>()));
    } else if (v.is<std::string>()) {
        const auto &s = v.get<std::string>();
        Game::Lua::PushLString(L, s.data(),
                               static_cast<unsigned int>(s.size()));
    } else if (v.is<picojson::array>()) {
        const auto &arr = v.get<picojson::array>();
        Game::Lua::NewTable(L);
        int i = 1;
        for (const auto &child : arr) {
            Game::Lua::PushNumber(L, static_cast<double>(i++));
            PicojsonToLua(L, child);
            Game::Lua::RawSet(L, -3);
        }
    } else if (v.is<picojson::object>()) {
        const auto &obj = v.get<picojson::object>();
        Game::Lua::NewTable(L);
        for (const auto &kv : obj) {
            Game::Lua::PushLString(L, kv.first.data(),
                                   static_cast<unsigned int>(kv.first.size()));
            PicojsonToLua(L, kv.second);
            Game::Lua::RawSet(L, -3);
        }
    } else {
        Game::Lua::PushNil(L);
    }
}

int __fastcall Script_SerializeJSON(void *L) {
    if (Game::Lua::GetTop(L) < 1) {
        Game::Lua::Error(L, "Usage: C_EncodingUtil.SerializeJSON(value)");
        return 0;
    }
    picojson::value v;
    if (!LuaToPicojson(L, 1, v, 0))
        return 0;
    const std::string out = v.serialize();
    Game::Lua::PushLString(L, out.data(),
                           static_cast<unsigned int>(out.size()));
    return 1;
}

int __fastcall Script_DeserializeJSON(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: C_EncodingUtil.DeserializeJSON(json)");
        return 0;
    }
    const char *src = Game::Lua::ToString(L, 1);
    const unsigned int srcLen = Game::Lua::StrLen(L, 1);
    if (src == nullptr)
        return 0;

    picojson::value v;
    std::string err;
    const char *endp = picojson::parse(v, src, src + srcLen, &err);
    if (!err.empty() || endp != src + srcLen)
        return 0;

    PicojsonToLua(L, v);
    return 1;
}

const Game::Doc::Field kSerializeArgs[] = {
    Game::Doc::Opt("value", "any", nullptr,
                   "Nil, a boolean, number or string, or a table of these. A table with keys "
                   "1 to n becomes an array; NaN and infinity become null."),
};
const Game::Doc::Field kSerializeRets[] = {
    Game::Doc::Opt("output", "string", nullptr,
                   "Nothing when the value holds a function or userdata, or tables nested "
                   "64 deep."),
};
const Game::Doc::Function kSerializeJSON{"Encodes a Lua value as JSON.", kSerializeArgs,
                                         kSerializeRets};

const Game::Doc::Field kDeserializeArgs[] = {
    Game::Doc::Req("source", "string"),
};
const Game::Doc::Field kDeserializeRets[] = {
    Game::Doc::Opt("value", "any", nullptr, "Nothing when the source is not valid JSON."),
};
const Game::Doc::Function kDeserializeJSON{"Decodes a JSON string into a Lua value.",
                                           kDeserializeArgs, kDeserializeRets};

void Register() {
    Game::Lua::RegisterTableFunction("C_EncodingUtil", "SerializeJSON",
                                     &Script_SerializeJSON, &kSerializeJSON);
    Game::Lua::RegisterTableFunction("C_EncodingUtil", "DeserializeJSON",
                                     &Script_DeserializeJSON, &kDeserializeJSON);
}

} // namespace

static const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace Encoding::JSON
