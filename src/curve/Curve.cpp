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

// C_CurveUtil and its curve objects, as the 3.80.2 client does them
// (docs/notes/curve-util-3802.md).
//
// THE OBJECTS. LuaCurveObject (scalar: float x, float y) and
// LuaColorCurveObject (float x, a colour of 8 bits a channel), both with
// LuaCurveObjectBase's GetType, SetType and HasSecretValues. At most 256
// points, kept sorted by x: AddPoint inserts before the points of the same x,
// SetPoints sorts with std::sort on `a.x < b.x` (MSVC's introsort: equal x keep
// their order up to 32 points). New objects and SetToDefaults: no points,
// Linear.
//
// EVALUATE. No points: 0 (colour: every channel 0). One point: its value.
// Otherwise the first point past x decides; below the first point or past the
// last the value is the end point's (no extrapolation). Linear interpolates,
// Step takes the last point at or before x, Cosine eases by (1 - cos(t pi)) / 2,
// Cubic is a uniform Catmull-Rom over the points from the second to the one
// before last (the end points only steer it: it is flat outside them), Cosine
// below four points. Colour curves run the same arithmetic per channel and pack
// the result by rounding, low byte, unclamped: a Cubic overshoot wraps.
//
// ERRORS. Argument errors are the generated readers' (lua/Arg.h). A method's
// own error is "<class>:<Method>(): <message>\nNo Lua Taint" (the pending
// error formatter 0x1405AC380 always adds the taint line; this client has no
// taint). The point tables copy the keys of Vector2DMixin / ColorMixin, as
// 3.80.2's pushers do; it takes the mixin from the calling function's
// environment, this from the globals. C_CurveUtil's three Evaluate* are not
// wrapped in a pcall there, so their argument errors name the function and
// carry the caller's position (Arg::RaiseNamed).
//
// Not ported: secret values (HasSecretValues is always false), the
// Enum.LuaCurveTypeMeta table, and game curves: this client has no
// ParameterCurve table, so EvaluateGameCurve gives 0 for every curve, as 3.80.2
// does for an ID outside its table.

#include "curve/Curve.h"

#include "Game.h"
#include "lua/Arg.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace Curve {

namespace {

namespace Lua = Game::Lua;
namespace Arg = Game::Lua::Arg;
namespace FU = Game::Lua::FullUserdata;

constexpr const char *kScalarKey = "ClassicAPI.LuaCurveObject";
constexpr const char *kColorKey = "ClassicAPI.LuaColorCurveObject";
constexpr const char *kScalarName = "LuaCurveObject";
constexpr const char *kColorName = "LuaColorCurveObject";
constexpr float kPi = 3.14159265358979323846f;

// ---- evaluation -------------------------------------------------------------------

template <typename P> auto UpperBound(const P *first, const P *last, float x) {
    return std::upper_bound(first, last, x, [](float v, const P &p) { return v < p.x; });
}

// One segment, a to b, for the value pair (va, vb) at x (0x1425499E0 /
// 0x142549A20). A segment of no width gives vb.
float Linear(float ax, float va, float bx, float vb, float x) {
    const float dx = bx - ax;
    return dx <= 0.0f ? vb : (x - ax) / dx * (vb - va) + va;
}

float Cosine(float ax, float va, float bx, float vb, float x) {
    const float dx = bx - ax;
    return dx > 0.0f ? (1.0f - std::cos((x - ax) / dx * kPi)) * 0.5f * (vb - va) + va : vb;
}

// Uniform Catmull-Rom between p1 and p2 (0x142549AA0).
float CatmullRom(float v0, float x1, float v1, float x2, float v2, float v3, float x) {
    const float dx = x2 - x1;
    if (dx <= 0.0f)
        return v1;
    const float t = (x - x1) / dx;
    return ((1.5f * v1 - 0.5f * v0 - 1.5f * v2 + 0.5f * v3) * t + (v0 - 2.5f * v1 + 2.0f * v2 - 0.5f * v3)) * t *
               t +
           (0.5f * v2 - 0.5f * v0) * t + v1;
}

// The interpolation every type shares, over points of any kind: `value(p,
// channel)` reads a point's value, `ends(p)` is what an end or a Step gives.
// Returns false when `out` should be the point `*at` as it is.
template <typename P, typename Value>
bool Interpolate(const std::vector<P> &points, uint8_t type, float x, Value value, int channel, float &out,
                 const P *&at) {
    const std::size_t n = points.size();
    const P *first = points.data();
    const P *last = first + n;
    if (type == TYPE_CUBIC && n >= 4) {
        const P *it = UpperBound(first + 1, last - 1, x);
        if (it == first + 1) {
            at = first + 1;
            return false;
        }
        if (it == last - 1) {
            at = last - 2;
            return false;
        }
        out = CatmullRom(value(it[-2], channel), it[-1].x, value(it[-1], channel), it[0].x, value(it[0], channel),
                         value(it[1], channel), x);
        return true;
    }
    const P *it = UpperBound(first, last, x);
    if (it == first) {
        at = first;
        return false;
    }
    if (it == last) {
        at = last - 1;
        return false;
    }
    const P &a = it[-1];
    const P &b = it[0];
    switch (type) {
    case TYPE_LINEAR:
        out = Linear(a.x, value(a, channel), b.x, value(b, channel), x);
        return true;
    case TYPE_STEP:
        at = &a;
        return false;
    case TYPE_COSINE:
    case TYPE_CUBIC: // fewer than four points
        out = Cosine(a.x, value(a, channel), b.x, value(b, channel), x);
        return true;
    default:
        at = nullptr;
        out = 0.0f;
        return true;
    }
}

uint8_t Channel(uint32_t color, int shift) { return static_cast<uint8_t>(color >> shift); }

// Unpack (0x142A65B80) and pack (0x142A65740: cvtss2si, round to nearest
// even, low byte, no clamp).
float Unpack(uint8_t byte) { return byte * (1.0f / 255.0f); }
uint8_t PackEvaluated(float c) { return static_cast<uint8_t>(static_cast<int>(std::nearbyint(c * 255.0f))); }

// An argument channel (0x140A6D740): clamped to [0, 1], then
// (int)(c * 255 + 0.5).
uint8_t PackArgument(float c) {
    c = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
    return static_cast<uint8_t>(static_cast<int>(c * 255.0f + 0.5f));
}

constexpr int kShiftR = 16, kShiftG = 8, kShiftB = 0, kShiftA = 24;

uint32_t Pack(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return static_cast<uint32_t>(b) << kShiftB | static_cast<uint32_t>(g) << kShiftG |
           static_cast<uint32_t>(r) << kShiftR | static_cast<uint32_t>(a) << kShiftA;
}

} // namespace

float Scalar::Evaluate(float x) const {
    if (points.empty())
        return 0.0f;
    if (points.size() == 1)
        return points[0].y;
    float out = 0.0f;
    const ScalarPoint *at = nullptr;
    if (Interpolate(points, type, x, [](const ScalarPoint &p, int) { return p.y; }, 0, out, at))
        return out;
    return at != nullptr ? at->y : 0.0f;
}

uint32_t Color::Evaluate(float x) const {
    if (points.empty())
        return 0;
    if (points.size() == 1)
        return points[0].color;
    const auto value = [](const ColorPoint &p, int shift) { return Unpack(Channel(p.color, shift)); };
    uint8_t bytes[4] = {};
    const int shifts[4] = {kShiftR, kShiftG, kShiftB, kShiftA};
    for (int i = 0; i < 4; ++i) {
        float out = 0.0f;
        const ColorPoint *at = nullptr;
        if (!Interpolate(points, type, x, value, shifts[i], out, at))
            return at != nullptr ? at->color : 0; // an end or a Step: the stored colour
        bytes[i] = PackEvaluated(out);
    }
    return Pack(bytes[0], bytes[1], bytes[2], bytes[3]);
}

namespace {

// ---- Lua values -------------------------------------------------------------------

// Pushes a new table holding every key of _G[mixin] (0x140A6FCF0 /
// 0x140AD1900). Raises when the mixin is not a table.
void PushMixinCopy(void *L, const char *mixin) {
    Lua::PushString(L, mixin);
    Lua::GetTable(L, Lua::GLOBALS_INDEX);
    if (Lua::Type(L, -1) != Lua::TYPE_TABLE) {
        char text[Arg::kErrorCap + 1];
        std::snprintf(text, sizeof text, "unable to find mixin or metatable (%s)", mixin);
        Arg::RaiseMessage(L, text);
        return;
    }
    const int m = Lua::GetTop(L);
    Lua::NewTable(L);
    const int t = Lua::GetTop(L);
    Lua::PushNil(L);
    while (Lua::Next(L, m) != 0) {
        Lua::PushValue(L, -2);
        Lua::PushValue(L, -2);
        Lua::RawSet(L, t);
        Lua::SetTop(L, -2);
    }
    Lua::Remove(L, m);
}

void PushColor(void *L, uint32_t color) {
    PushMixinCopy(L, "ColorMixin");
    Lua::SetFieldNumber(L, "r", Unpack(Channel(color, kShiftR)));
    Lua::SetFieldNumber(L, "g", Unpack(Channel(color, kShiftG)));
    Lua::SetFieldNumber(L, "b", Unpack(Channel(color, kShiftB)));
    Lua::SetFieldNumber(L, "a", Unpack(Channel(color, kShiftA)));
}

void PushPoint(void *L, const ScalarPoint &p) {
    PushMixinCopy(L, "Vector2DMixin");
    Lua::SetFieldNumber(L, "x", p.x);
    Lua::SetFieldNumber(L, "y", p.y);
}

void PushPoint(void *L, const ColorPoint &p) {
    Lua::NewTable(L);
    Lua::SetFieldNumber(L, "x", p.x);
    Lua::PushString(L, "y");
    PushColor(L, p.color);
    Lua::SetTable(L, -3);
}

// A number field read as 0x140A63B60 does: a finite number within +-FLT_MAX.
bool FloatField(void *L, int table, const char *key, float &out) {
    Lua::PushString(L, key);
    Lua::GetTable(L, table);
    const double v = Lua::IsNumber(L, -1) ? Lua::ToNumber(L, -1) : NAN;
    Lua::SetTop(L, -2);
    if (!(std::isfinite(v) && v >= -FLT_MAX && v <= FLT_MAX))
        return false;
    out = static_cast<float>(v);
    return true;
}

// A colour argument (0x140A6D740): a table with r, g, b and a, each required.
// Anything else is the usage error at its slot (with `field` as Current Field).
bool ReadColor(void *L, int slot, const char *usage, uint32_t &out, Arg::Error &e, const char *field = nullptr) {
    float c[4];
    if (Lua::Type(L, slot) != Lua::TYPE_TABLE || !FloatField(L, slot, "r", c[0]) || !FloatField(L, slot, "g", c[1]) ||
        !FloatField(L, slot, "b", c[2]) || !FloatField(L, slot, "a", c[3]))
        return Arg::Usage(L, slot, usage, &e, field);
    out = Pack(PackArgument(c[0]), PackArgument(c[1]), PackArgument(c[2]), PackArgument(c[3]));
    return true;
}

// The method's own error, into `e`.
bool MethodError(Arg::Error &e, const char *className, const char *method, const char *message) {
    std::snprintf(e.text, sizeof e.text, "%s:%s(): %s\nNo Lua Taint", className, method, message);
    e.arg = 0;
    return false;
}

bool TooMany(Arg::Error &e, const char *className, const char *method, std::size_t count) {
    char message[128];
    std::snprintf(message, sizeof message,
                  "attempted to assign too many points to a curve (expected no more than %llu points)",
                  static_cast<unsigned long long>(count));
    return MethodError(e, className, method, message);
}

// Each Impl fills `e` and returns false on an error, which Raising raises once
// the Impl's locals are gone.
template <bool (*Impl)(void *, Arg::Error &)> int __fastcall Raising(void *L) {
    Arg::Error e;
    const int top = Lua::GetTop(L);
    if (!Impl(L, e))
        Arg::Raise(L, e);
    return Lua::GetTop(L) - top;
}

// ---- the two classes, one template ----------------------------------------------

template <typename C> struct Traits;
template <> struct Traits<Scalar> {
    using Point = ScalarPoint;
    static constexpr const char *kKey = kScalarKey;
    static constexpr const char *kName = kScalarName;
};
template <> struct Traits<Color> {
    using Point = ColorPoint;
    static constexpr const char *kKey = kColorKey;
    static constexpr const char *kName = kColorName;
};

template <typename C> C *Self(void *L, const char *usage, Arg::Error &e) {
    auto *c = static_cast<C *>(FU::To(L, 1, Traits<C>::kKey));
    if (c == nullptr)
        Arg::Usage(L, 1, usage, &e);
    return c;
}

// Either class (the base's self reader 0x140D28350).
Base *AnySelf(void *L, const char *usage, Arg::Error &e) {
    if (auto *s = FU::To(L, 1, kScalarKey))
        return static_cast<Scalar *>(s);
    if (auto *c = FU::To(L, 1, kColorKey))
        return static_cast<Color *>(c);
    Arg::Usage(L, 1, usage, &e);
    return nullptr;
}

// AddPoint's arguments after self.
bool ReadAddPoint(void *L, const char *usage, ScalarPoint &p, Arg::Error &e) {
    // The vector2 reader (0x140A92C90): two numbers, no finite check.
    if (!Lua::IsNumber(L, 2) || !Lua::IsNumber(L, 3))
        return Arg::Usage(L, 2, usage, &e);
    p.x = static_cast<float>(Lua::ToNumber(L, 2));
    p.y = static_cast<float>(Lua::ToNumber(L, 3));
    return true;
}

bool ReadAddPoint(void *L, const char *usage, ColorPoint &p, Arg::Error &e) {
    return Arg::FiniteFloat(L, 2, usage, p.x, false, &e) && ReadColor(L, 3, usage, p.color, e);
}

// One SetPoints element at `slot`.
bool ReadElement(void *L, int slot, const char *usage, ScalarPoint &p, Arg::Error &e) {
    // 0x140A6F5B0: a table with x and y; anything wrong is the usage error at the element.
    if (Lua::Type(L, slot) != Lua::TYPE_TABLE || !FloatField(L, slot, "x", p.x) || !FloatField(L, slot, "y", p.y))
        return Arg::Usage(L, slot, usage, &e);
    return true;
}

bool ReadElement(void *L, int slot, const char *usage, ColorPoint &p, Arg::Error &e) {
    if (Lua::Type(L, slot) != Lua::TYPE_TABLE)
        return Arg::Usage(L, slot, usage, &e);
    if (!Arg::FieldFiniteFloat(L, slot, "x", usage, p.x, false, &e))
        return false;
    Lua::PushString(L, "y");
    Lua::GetTable(L, slot);
    const bool ok = ReadColor(L, Lua::GetTop(L), usage, p.color, e, "y");
    Lua::SetTop(L, -2);
    return ok;
}

template <typename C> bool AddPointImpl(void *L, Arg::Error &e) {
    constexpr const char *kUsage = "Usage: self:AddPoint(point)";
    C *c = Self<C>(L, kUsage, e);
    typename Traits<C>::Point p{};
    if (c == nullptr || !ReadAddPoint(L, kUsage, p, e))
        return false;
    if (c->points.size() >= kMaxPoints)
        return TooMany(e, Traits<C>::kName, "AddPoint", c->points.size());
    const auto at = std::lower_bound(c->points.begin(), c->points.end(), p.x,
                                     [](const typename Traits<C>::Point &q, float x) { return q.x < x; });
    c->points.insert(at, p);
    return true;
}

template <typename C> bool SetPointsImpl(void *L, Arg::Error &e) {
    constexpr const char *kUsage = "Usage: self:SetPoints(point)";
    C *c = Self<C>(L, kUsage, e);
    if (c == nullptr)
        return false;
    if (Lua::Type(L, 2) != Lua::TYPE_TABLE)
        return Arg::Usage(L, 2, kUsage, &e);
    // 3.80.2 reads the list by its length; this client's C API reads it to its first nil.
    std::vector<typename Traits<C>::Point> list;
    for (int i = 1;; ++i) {
        Lua::RawGetI(L, 2, i);
        const int slot = Lua::GetTop(L);
        if (Lua::Type(L, slot) == Lua::TYPE_NIL) {
            Lua::SetTop(L, slot - 1);
            break;
        }
        typename Traits<C>::Point p{};
        const bool ok = ReadElement(L, slot, kUsage, p, e);
        Lua::SetTop(L, slot - 1);
        if (!ok)
            return false;
        list.push_back(p);
    }
    if (list.size() > kMaxPoints)
        return TooMany(e, Traits<C>::kName, "SetPoints", list.size());
    std::sort(list.begin(), list.end(),
              [](const typename Traits<C>::Point &a, const typename Traits<C>::Point &b) { return a.x < b.x; });
    c->points = std::move(list);
    return true;
}

template <typename C> bool ClearPointsImpl(void *L, Arg::Error &e) {
    C *c = Self<C>(L, "Usage: self:ClearPoints()", e);
    if (c != nullptr)
        c->points.clear();
    return c != nullptr;
}

template <typename C> bool SetToDefaultsImpl(void *L, Arg::Error &e) {
    C *c = Self<C>(L, "Usage: self:SetToDefaults()", e);
    if (c == nullptr)
        return false;
    c->points.clear();
    c->type = TYPE_LINEAR;
    return true;
}

template <typename C> bool CopyImpl(void *L, Arg::Error &e) {
    C *c = Self<C>(L, "Usage: local curve = self:Copy()", e);
    if (c == nullptr)
        return false;
    auto *copy = new C;
    copy->type = c->type;
    copy->points = c->points;
    FU::Push(L, Traits<C>::kKey, copy);
    return true;
}

template <typename C> bool GetPointCountImpl(void *L, Arg::Error &e) {
    C *c = Self<C>(L, "Usage: local count = self:GetPointCount()", e);
    if (c != nullptr)
        Lua::PushNumber(L, static_cast<double>(c->points.size()));
    return c != nullptr;
}

template <typename C> bool GetPointImpl(void *L, Arg::Error &e) {
    constexpr const char *kUsage = "Usage: local point = self:GetPoint(index)";
    C *c = Self<C>(L, kUsage, e);
    uint32_t index = 0;
    if (c == nullptr || !Arg::Index(L, 2, kUsage, index, &e))
        return false;
    if (index >= c->points.size())
        Lua::PushNil(L);
    else
        PushPoint(L, c->points[index]);
    return true;
}

template <typename C> bool GetPointsImpl(void *L, Arg::Error &e) {
    C *c = Self<C>(L, "Usage: local point = self:GetPoints()", e);
    if (c == nullptr)
        return false;
    Lua::NewTable(L);
    for (std::size_t i = 0; i < c->points.size(); ++i) {
        PushPoint(L, c->points[i]);
        Lua::RawSetI(L, -2, static_cast<int>(i + 1));
    }
    return true;
}

template <typename C> bool RemovePointImpl(void *L, Arg::Error &e) {
    constexpr const char *kUsage = "Usage: self:RemovePoint(index)";
    C *c = Self<C>(L, kUsage, e);
    uint32_t index = 0;
    if (c == nullptr || !Arg::Index(L, 2, kUsage, index, &e))
        return false;
    if (index >= c->points.size()) {
        char message[96];
        std::snprintf(message, sizeof message, "attempted to remove an invalid curve point (index %u out of range)",
                      index);
        return MethodError(e, Traits<C>::kName, "RemovePoint", message);
    }
    c->points.erase(c->points.begin() + index);
    return true;
}

bool EvaluateScalarImpl(void *L, Arg::Error &e) {
    constexpr const char *kUsage = "Usage: local y = self:Evaluate(x)";
    Scalar *c = Self<Scalar>(L, kUsage, e);
    float x = 0.0f;
    if (c == nullptr || !Arg::FiniteFloat(L, 2, kUsage, x, false, &e))
        return false;
    Lua::PushNumber(L, c->Evaluate(x));
    return true;
}

bool EvaluateColorImpl(void *L, Arg::Error &e) {
    constexpr const char *kUsage = "Usage: local y = self:Evaluate(x)";
    Color *c = Self<Color>(L, kUsage, e);
    float x = 0.0f;
    if (c == nullptr || !Arg::FiniteFloat(L, 2, kUsage, x, false, &e))
        return false;
    PushColor(L, c->Evaluate(x));
    return true;
}

bool EvaluateUnpackedImpl(void *L, Arg::Error &e) {
    constexpr const char *kUsage = "Usage: local r, g, b, a = self:EvaluateUnpacked(x)";
    Color *c = Self<Color>(L, kUsage, e);
    float x = 0.0f;
    if (c == nullptr || !Arg::FiniteFloat(L, 2, kUsage, x, false, &e))
        return false;
    const uint32_t color = c->Evaluate(x);
    Lua::PushNumber(L, Unpack(Channel(color, kShiftR)));
    Lua::PushNumber(L, Unpack(Channel(color, kShiftG)));
    Lua::PushNumber(L, Unpack(Channel(color, kShiftB)));
    Lua::PushNumber(L, Unpack(Channel(color, kShiftA)));
    return true;
}

// ---- the base's methods ----------------------------------------------------------

bool GetTypeImpl(void *L, Arg::Error &e) {
    Base *c = AnySelf(L, "Usage: local curveType = self:GetType()", e);
    if (c != nullptr)
        Lua::PushNumber(L, c->type);
    return c != nullptr;
}

// The curve type reader (0x140D287C0): a number whose truncated low byte is 0 to 3.
bool SetTypeImpl(void *L, Arg::Error &e) {
    constexpr const char *kUsage = "Usage: self:SetType(type)";
    Base *c = AnySelf(L, kUsage, e);
    if (c == nullptr)
        return false;
    if (!Lua::IsNumber(L, 2))
        return Arg::Usage(L, 2, kUsage, &e);
    const double v = Lua::ToNumber(L, 2);
    const int64_t whole = v > -9223372036854775808.0 && v < 9223372036854775808.0 ? static_cast<int64_t>(v)
                                                                                   : INT64_MIN;
    const uint8_t byte = static_cast<uint8_t>(whole);
    if (byte > TYPE_CUBIC)
        return Arg::Usage(L, 2, kUsage, &e);
    c->type = byte;
    return true;
}

bool HasSecretValuesImpl(void *L, Arg::Error &e) {
    Base *c = AnySelf(L, "Usage: local hasSecretValues = self:HasSecretValues()", e);
    if (c != nullptr)
        Lua::PushBool(L, false);
    return c != nullptr;
}

const Lua::Boxed::Method kScalarMethods[] = {
    {"AddPoint", &Raising<&AddPointImpl<Scalar>>},
    {"ClearPoints", &Raising<&ClearPointsImpl<Scalar>>},
    {"Copy", &Raising<&CopyImpl<Scalar>>},
    {"Evaluate", &Raising<&EvaluateScalarImpl>},
    {"GetPoint", &Raising<&GetPointImpl<Scalar>>},
    {"GetPointCount", &Raising<&GetPointCountImpl<Scalar>>},
    {"GetPoints", &Raising<&GetPointsImpl<Scalar>>},
    {"RemovePoint", &Raising<&RemovePointImpl<Scalar>>},
    {"SetPoints", &Raising<&SetPointsImpl<Scalar>>},
    {"SetToDefaults", &Raising<&SetToDefaultsImpl<Scalar>>},
    {"GetType", &Raising<&GetTypeImpl>},
    {"HasSecretValues", &Raising<&HasSecretValuesImpl>},
    {"SetType", &Raising<&SetTypeImpl>},
};

const Lua::Boxed::Method kColorMethods[] = {
    {"AddPoint", &Raising<&AddPointImpl<Color>>},
    {"ClearPoints", &Raising<&ClearPointsImpl<Color>>},
    {"Copy", &Raising<&CopyImpl<Color>>},
    {"Evaluate", &Raising<&EvaluateColorImpl>},
    {"EvaluateUnpacked", &Raising<&EvaluateUnpackedImpl>},
    {"GetPoint", &Raising<&GetPointImpl<Color>>},
    {"GetPointCount", &Raising<&GetPointCountImpl<Color>>},
    {"GetPoints", &Raising<&GetPointsImpl<Color>>},
    {"RemovePoint", &Raising<&RemovePointImpl<Color>>},
    {"SetPoints", &Raising<&SetPointsImpl<Color>>},
    {"SetToDefaults", &Raising<&SetToDefaultsImpl<Color>>},
    {"GetType", &Raising<&GetTypeImpl>},
    {"HasSecretValues", &Raising<&HasSecretValuesImpl>},
    {"SetType", &Raising<&SetTypeImpl>},
};

// ---- C_CurveUtil ------------------------------------------------------------------

int __fastcall Script_CreateCurve(void *L) {
    FU::Push(L, kScalarKey, new Scalar);
    return 1;
}

int __fastcall Script_CreateColorCurve(void *L) {
    FU::Push(L, kColorKey, new Color);
    return 1;
}

// EvaluateColorFromBoolean (0x140BFA880): a new ColorMixin of the chosen colour,
// both read (and so quantised) first.
int __fastcall Script_EvaluateColorFromBoolean(void *L) {
    constexpr const char *kUsage =
        "Usage: local value = C_CurveUtil.EvaluateColorFromBoolean(boolean, valueIfTrue, valueIfFalse)";
    Arg::Error e;
    bool which = false;
    uint32_t ifTrue = 0, ifFalse = 0;
    if (!Arg::Bool(L, 1, kUsage, which, false, &e) || !ReadColor(L, 2, kUsage, ifTrue, e) ||
        !ReadColor(L, 3, kUsage, ifFalse, e)) {
        Arg::RaiseNamed(L, e);
        return 0;
    }
    PushColor(L, which ? ifTrue : ifFalse);
    return 1;
}

// A SingleColorValue (0x140A6EFD0): a number (not a numeric string), clamped to
// [0, 1] (NaN is 0), as a byte.
bool ReadColorValue(void *L, int slot, const char *usage, uint8_t &out, Arg::Error &e) {
    if (Lua::Type(L, slot) != Lua::TYPE_NUMBER)
        return Arg::Usage(L, slot, usage, &e);
    double v = Lua::ToNumber(L, slot);
    v = v > 0.0 ? (v < 1.0 ? v : 1.0) : 0.0;
    out = static_cast<uint8_t>(static_cast<int>(v * 255.0 + 0.5));
    return true;
}

// EvaluateColorValueFromBoolean (0x140BFAB70): the chosen value as byte / 255.
int __fastcall Script_EvaluateColorValueFromBoolean(void *L) {
    constexpr const char *kUsage =
        "Usage: local value = C_CurveUtil.EvaluateColorValueFromBoolean(boolean, valueIfTrue, valueIfFalse)";
    Arg::Error e;
    bool which = false;
    uint8_t ifTrue = 0, ifFalse = 0;
    if (!Arg::Bool(L, 1, kUsage, which, false, &e) || !ReadColorValue(L, 2, kUsage, ifTrue, e) ||
        !ReadColorValue(L, 3, kUsage, ifFalse, e)) {
        Arg::RaiseNamed(L, e);
        return 0;
    }
    Lua::PushNumber(L, Unpack(which ? ifTrue : ifFalse));
    return 1;
}

// EvaluateGameCurve (0x140BFACB0): an int32 curve ID and a float x. This client
// has no game curves, so every ID is outside the table: 0.
int __fastcall Script_EvaluateGameCurve(void *L) {
    constexpr const char *kUsage = "Usage: local y = C_CurveUtil.EvaluateGameCurve(curveID, x)";
    Arg::Error e;
    float x = 0.0f;
    const double id = Lua::IsNumber(L, 1) ? Lua::ToNumber(L, 1) : NAN;
    if (Lua::Type(L, 1) <= Lua::TYPE_NIL) {
        Arg::Usage(L, 1, kUsage, &e);
        Arg::RaiseNamed(L, e);
        return 0;
    }
    if (!(std::isfinite(id))) {
        e.arg = 1;
        std::snprintf(e.what, sizeof e.what, "must be a finite number");
        e.usage = kUsage;
        Arg::RaiseNamed(L, e);
        return 0;
    }
    if (!(id >= -2147483648.0 && id <= 2147483647.0)) {
        e.arg = 1;
        std::snprintf(e.what, sizeof e.what, "outside of expected range -2147483648 to 2147483647");
        e.usage = kUsage;
        Arg::RaiseNamed(L, e);
        return 0;
    }
    if (!Arg::FiniteFloat(L, 2, kUsage, x, false, &e)) {
        Arg::RaiseNamed(L, e);
        return 0;
    }
    Lua::PushNumber(L, 0.0);
    return 1;
}

// ---- registration -----------------------------------------------------------------

const Lua::EnumIntegerEntry kTypes[] = {
    {"Linear", TYPE_LINEAR},
    {"Step", TYPE_STEP},
    {"Cosine", TYPE_COSINE},
    {"Cubic", TYPE_CUBIC},
};

using Game::Doc::Req;

const Game::Doc::Field kCurveRet[] = {Req("curve", "LuaCurveObject")};
const Game::Doc::Field kColorCurveRet[] = {Req("curve", "LuaColorCurveObject")};
const Game::Doc::Function kCreateCurve{"Creates an empty curve of numbers.", {}, kCurveRet};
const Game::Doc::Function kCreateColorCurve{"Creates an empty curve of colors.", {}, kColorCurveRet};

const Game::Doc::Field kColorFromBooleanArgs[] = {
    Req("boolean", "bool"),
    Req("valueIfTrue", "colorRGBA"),
    Req("valueIfFalse", "colorRGBA"),
};
const Game::Doc::Field kColorRet[] = {Req("value", "colorRGBA")};
const Game::Doc::Function kEvaluateColorFromBoolean{
    "Returns a new color: valueIfTrue when boolean is true, else valueIfFalse.", kColorFromBooleanArgs, kColorRet};

const Game::Doc::Field kColorValueFromBooleanArgs[] = {
    Req("boolean", "bool"),
    Req("valueIfTrue", "SingleColorValue"),
    Req("valueIfFalse", "SingleColorValue"),
};
const Game::Doc::Field kColorValueRet[] = {Req("value", "SingleColorValue")};
const Game::Doc::Function kEvaluateColorValueFromBoolean{
    "Returns valueIfTrue when boolean is true, else valueIfFalse, as a color channel value.",
    kColorValueFromBooleanArgs, kColorValueRet};

const Game::Doc::Field kGameCurveArgs[] = {
    Req("curveID", "number"),
    Req("x", "number"),
};
const Game::Doc::Field kGameCurveRet[] = {Req("y", "number")};
const Game::Doc::Function kEvaluateGameCurve{"Returns the value of a game curve at x.", kGameCurveArgs,
                                             kGameCurveRet};

void RegisterLuaFunctions() {
    void *L = Lua::State();
    FU::CreateClass(L, kScalarKey, kScalarName, kScalarMethods, sizeof kScalarMethods / sizeof kScalarMethods[0]);
    FU::CreateClass(L, kColorKey, kColorName, kColorMethods, sizeof kColorMethods / sizeof kColorMethods[0]);
    Lua::RegisterTableFunction("C_CurveUtil", "CreateColorCurve", &Script_CreateColorCurve, &kCreateColorCurve);
    Lua::RegisterTableFunction("C_CurveUtil", "CreateCurve", &Script_CreateCurve, &kCreateCurve);
    Lua::RegisterTableFunction("C_CurveUtil", "EvaluateColorFromBoolean", &Script_EvaluateColorFromBoolean,
                               &kEvaluateColorFromBoolean);
    Lua::RegisterTableFunction("C_CurveUtil", "EvaluateColorValueFromBoolean",
                               &Script_EvaluateColorValueFromBoolean, &kEvaluateColorValueFromBoolean);
    Lua::RegisterTableFunction("C_CurveUtil", "EvaluateGameCurve", &Script_EvaluateGameCurve, &kEvaluateGameCurve);
    Lua::RegisterIntegerEnum("Enum", "LuaCurveType", kTypes, "CurveUtil");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterLuaFunctions};

} // namespace

Scalar *ToScalar(void *L, int idx) { return static_cast<Scalar *>(FU::To(L, idx, kScalarKey)); }

void Push(void *L, Scalar *c) { FU::Push(L, kScalarKey, c); }

} // namespace Curve
