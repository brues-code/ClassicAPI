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

// 4.3.4's CSimplePathAnim and CSimpleControlPoint (wowdecomp15595
// CSimplePathAnim.cpp, CSimpleControlPoint.cpp and their _Scripts.cpp).
//
// A Path moves the region through its control points: each point is an
// offset from where the region is, the points are spread evenly over the
// animation's progress (point i of n at (i + 1) / n), and the path starts
// at offset 0. Between points the offset is a straight line, or with the
// SMOOTH curve a Catmull-Rom spline. The offset goes through the same
// AddAnimTranslation as a Translation.

#include "animation/Classes.h"
#include "animation/Region.h"
#include "animation/Xml.h"

#include "Game.h"
#include "baselib/Ascii.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace Animation {

// ---- CSimpleControlPoint ------------------------------------------------------------

ControlPoint::ControlPoint(Anim *path, const char *name) : Object(s_class, path, name) {
    if (path != nullptr)
        path->controlPoints.push_back(this);
}

ControlPoint::~ControlPoint() = default;

void ControlPoint::SetOffset(float x, float y) {
    if (x * x + y * y <= 0.00000023841858f) {
        offsetX = 0.0f;
        offsetY = 0.0f;
    } else {
        offsetX = x;
        offsetY = y;
    }
}

void ControlPoint::SetOrder(int value, bool notify, bool wrap) {
    const int oldOrder = order;
    if (wrap)
        order = value < 0 || value > 99 ? 0 : value;
    else
        order = value < 0 ? 0 : (value > 99 ? 99 : value);
    if (parent != nullptr && notify && oldOrder != order)
        GetPath()->NotifyControlPointOrderChanged(this);
}

bool ControlPoint::ChangeParent(Anim *path) {
    if (path == nullptr || GetPath() == path)
        return false;
    // LinkToTail unlinks the point from the old path's list first.
    Anim *old = GetPath();
    if (old != nullptr)
        old->controlPoints.erase(std::remove(old->controlPoints.begin(), old->controlPoints.end(), this),
                                 old->controlPoints.end());
    path->controlPoints.push_back(this);
    if (old != nullptr)
        old->NotifyControlPointRemoved(this);
    parent = path;
    path->NotifyControlPointAdded(this);
    return true;
}

// ---- CSimplePathAnim -------------------------------------------------------------------

int Anim::GetMaxControlPointOrder() const { return sortedPoints.empty() ? -1 : sortedPoints.back()->order; }

int Anim::FindControlPoint(int value) const {
    if (value == -1)
        return -1;
    int low = 0;
    int high = static_cast<int>(sortedPoints.size()) - 1;
    while (low <= high) {
        const int middle = static_cast<int>(static_cast<unsigned>(high + low) >> 1);
        const ControlPoint *point = sortedPoints[middle];
        if (point == nullptr)
            break; // 4.3.4 spins here; the array has no holes after BuildSpline
        if (point->order < value)
            low = middle + 1;
        else if (point->order > value)
            high = middle - 1;
        else
            return middle;
    }
    return -1;
}

void Anim::UpdateControlPointPositions() {
    const size_t count = sortedPoints.size();
    if (count == 0)
        return;
    const float step = 1.0f / static_cast<float>(count);
    for (size_t i = 0; i < count; ++i) {
        float position = static_cast<float>(i + 1) * step;
        position = position < 0.0f ? 0.0f : (position >= 1.0f ? 1.0f : position);
        sortedPoints[i]->position = position;
    }
}

int Anim::RegisterSplineControlPoint(ControlPoint *point) {
    const int count = static_cast<int>(sortedPoints.size());
    if (count == 0) {
        sortedPoints.push_back(point);
        return 0;
    }
    int index = count;
    for (int i = 0; i < count; ++i) {
        ControlPoint *existing = sortedPoints[i];
        if (existing == nullptr) {
            sortedPoints[i] = point;
            return i;
        }
        if (point->order == existing->order)
            return i;
        if (point->order < existing->order) {
            index = i;
            break;
        }
    }
    sortedPoints.insert(sortedPoints.begin() + index, point);
    return index;
}

void Anim::BuildSpline() {
    Stop(false);
    if (controlPoints.empty()) {
        sortedPoints.clear();
        return;
    }
    std::fill(sortedPoints.begin(), sortedPoints.end(), nullptr);
    // Points take their order in link order; a point without one, or with
    // one below the next free order, takes the next free order.
    int nextOrder = 0;
    for (ControlPoint *point : controlPoints) {
        int value = point->order;
        if (value == -1 || value < nextOrder) {
            value = nextOrder++;
            point->order = value < 0 ? 0 : value;
        } else {
            nextOrder = value + 1;
        }
        if (nextOrder > 99)
            nextOrder = 0;
        RegisterSplineControlPoint(point);
    }
    while (!sortedPoints.empty() && sortedPoints.back() == nullptr)
        sortedPoints.pop_back();
}

void Anim::NotifyControlPointAdded(ControlPoint *point) {
    if (point == nullptr)
        return;
    const int index = FindControlPoint(point->order);
    if (index < 0) {
        if (RegisterSplineControlPoint(point) >= 0)
            UpdateControlPointPositions();
        return;
    }
    // The order is taken: the point goes before the one that has it and
    // takes the order; BuildSpline moves the rest up.
    ControlPoint *existing = sortedPoints[index];
    controlPoints.erase(std::remove(controlPoints.begin(), controlPoints.end(), point), controlPoints.end());
    controlPoints.insert(std::find(controlPoints.begin(), controlPoints.end(), existing), point);
    BuildSpline();
    UpdateControlPointPositions();
}

void Anim::NotifyControlPointRemoved(ControlPoint *point) {
    if (point != nullptr && FindControlPoint(point->order) >= 0) {
        BuildSpline();
        UpdateControlPointPositions();
    }
}

void Anim::NotifyControlPointOrderChanged(ControlPoint *point) {
    if (point == nullptr)
        return;
    const int index = FindControlPoint(point->order);
    if (index >= 0 && sortedPoints[index] != point) {
        ControlPoint *existing = sortedPoints[index];
        controlPoints.erase(std::remove(controlPoints.begin(), controlPoints.end(), point), controlPoints.end());
        controlPoints.insert(std::find(controlPoints.begin(), controlPoints.end(), existing), point);
    }
    BuildSpline();
    UpdateControlPointPositions();
}

void Anim::ApplyPath(float progress) {
    const int count = static_cast<int>(sortedPoints.size());
    if (count == 0)
        return;
    void *region = GetRegionParent();
    if (region == nullptr)
        return;

    const float t = std::fabs(progress);
    // The segment: 4.3.4 converts count * t with the 2^16 + x float trick,
    // which rounds to 1/128 before it truncates. Kept, so a progress just
    // under a point's lands on the same segment.
    const float scaled = static_cast<float>(count) * t;
    const float biased = std::fabs(scaled) + 65536.0f;
    int32_t bits;
    std::memcpy(&bits, &biased, sizeof(bits));
    uint32_t scaledBits;
    std::memcpy(&scaledBits, &scaled, sizeof(scaledBits));
    const int32_t sign = static_cast<int32_t>(scaledBits >> 31);
    const int index = static_cast<int16_t>((((bits >> 7) & 0x7FFF) ^ sign) - sign);

    float x = 0.0f, y = 0.0f;
    const auto offsetOf = [this](int i, float &ox, float &oy) {
        ox = sortedPoints[i]->offsetX;
        oy = sortedPoints[i]->offsetY;
    };
    if (static_cast<unsigned>(index) >= static_cast<unsigned>(count)) {
        offsetOf(count - 1, x, y);
    } else if (curve && count > 1) {
        // Catmull-Rom through p1 -> p2; the start is offset 0.
        float p0x = 0.0f, p0y = 0.0f, p1x = 0.0f, p1y = 0.0f, p2x, p2y, p3x, p3y, t0, t1;
        if (index != 0) {
            offsetOf(index - 1, p1x, p1y);
            t0 = sortedPoints[index - 1]->position;
            offsetOf(index, p2x, p2y);
            t1 = sortedPoints[index]->position;
            if (index != 1)
                offsetOf(index - 2, p0x, p0y);
            if (index == count - 1) {
                p3x = p2x * 2.0f - p1x;
                p3y = p2y * 2.0f - p1y;
            } else {
                offsetOf(index + 1, p3x, p3y);
            }
        } else {
            t0 = 0.0f;
            offsetOf(0, p2x, p2y);
            t1 = sortedPoints[0]->position;
            p0x = -p2x;
            p0y = -p2y;
            offsetOf(1, p3x, p3y);
        }
        const float f = (t - t0) / (t1 - t0);
        const float f2 = f * f;
        const float f3 = f2 * f;
        const float ax = -p0x + p1x * 3.0f - p2x * 3.0f + p3x;
        const float ay = -p0y + p1y * 3.0f - p2y * 3.0f + p3y;
        const float bx = p0x * 2.0f - p1x * 5.0f + p2x * 4.0f - p3x;
        const float by = p0y * 2.0f - p1y * 5.0f + p2y * 4.0f - p3y;
        const float cx = -p0x + p2x;
        const float cy = -p0y + p2y;
        x = (cx * f + p1x * 2.0f + bx * f2 + ax * f3) * 0.5f;
        y = (cy * f + p1y * 2.0f + by * f2 + ay * f3) * 0.5f;
    } else {
        float ax = 0.0f, ay = 0.0f, bx, by, t0;
        if (index != 0) {
            offsetOf(index - 1, ax, ay);
            t0 = sortedPoints[index - 1]->position;
        } else {
            t0 = 0.0f;
        }
        offsetOf(index, bx, by);
        const float t1 = sortedPoints[index]->position;
        const float f = (t - t0) / (t1 - t0);
        x = ax + (bx - ax) * f;
        y = f * (by - ay) + ay;
    }

    if (progress >= 0.0f)
        Region::AddAnimTranslation(region, target, x, y);
    else
        Region::AddAnimTranslation(region, target, -x, -y);
}

// ---- Lua methods ---------------------------------------------------------------------

namespace {

Anim *PathSelf(void *L) { return static_cast<Anim *>(Resolve(L, 1, Anim::s_pathClass)); }
ControlPoint *PointSelf(void *L) { return static_cast<ControlPoint *>(Resolve(L, 1, ControlPoint::s_class)); }

// LoadXML.cpp's s_curveTypes.
bool StringToCurve(const char *text, bool &curve) {
    if (Ascii::EqualCI(text, "NONE")) {
        curve = false;
        return true;
    }
    if (Ascii::EqualCI(text, "SMOOTH")) {
        curve = true;
        return true;
    }
    return false;
}

int __fastcall Script_SetCurve(void *L) {
    Anim *path = PathSelf(L);
    if (path == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:SetCurve(\"curveType\")", path->DisplayName());
        return 0;
    }
    if (!StringToCurve(Game::Lua::ToString(L, 2), path->curve))
        Game::Lua::Error(L, "%s:SetCurve(): Unknown curve type specified", path->DisplayName());
    return 0;
}

int __fastcall Script_GetCurve(void *L) {
    Anim *path = PathSelf(L);
    if (path == nullptr)
        return 0;
    Game::Lua::PushString(L, path->curve ? "SMOOTH" : "NONE");
    return 1;
}

int __fastcall Script_GetMaxOrder(void *L) {
    Anim *path = PathSelf(L);
    if (path == nullptr)
        return 0;
    Game::Lua::PushNumber(L, path->GetMaxControlPointOrder() + 1);
    return 1;
}

int __fastcall Script_GetControlPoints(void *L) {
    Anim *path = PathSelf(L);
    if (path == nullptr)
        return 0;
    const int count = static_cast<int>(path->sortedPoints.size());
    if (count == 0)
        return 0;
    if (!Game::Lua::CheckStack(L, count)) {
        Game::Lua::Error(L, "%s:GetControlPoints(): Stack overflow", path->DisplayName());
        return 0;
    }
    for (ControlPoint *point : path->sortedPoints)
        Push(L, point);
    return count;
}

int __fastcall Script_CreateControlPoint(void *L) {
    Anim *path = PathSelf(L);
    if (path == nullptr)
        return 0;
    const char *name = Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : nullptr;
    const void *templateNode = nullptr;
    if (Game::Lua::Type(L, 3) == Game::Lua::TYPE_STRING) {
        const char *inherits = Game::Lua::ToString(L, 3);
        templateNode = Xml::Template(inherits);
        if (templateNode == nullptr) {
            Game::Lua::Error(L, "%s:CreateControlPoint(): Couldn't find inherited node \"%s\"", path->DisplayName(),
                             inherits);
            return 0;
        }
    }
    ControlPoint *point = new ControlPoint(path, name);
    if (templateNode != nullptr)
        Xml::LoadControlPoint(point, templateNode, nullptr);
    if (Game::Lua::IsNumber(L, 4))
        point->SetOrder(static_cast<int>(Game::Lua::ToNumber(L, 4)) - 1, false, false);
    // The decompiled CreateControlPoint passes the highest order itself,
    // which takes the last point's place and reverses points made one after
    // another; one past it, as SetParent does, puts the point last.
    if (point->order == -1)
        point->SetOrder(path->GetMaxControlPointOrder() + 1, false, true);
    path->NotifyControlPointAdded(point);
    Push(L, point);
    return 1;
}

int __fastcall Script_PointSetParent(void *L) {
    ControlPoint *point = PointSelf(L);
    if (point == nullptr)
        return 0;
    if (Game::Lua::Type(L, 2) == Game::Lua::TYPE_NIL) {
        Game::Lua::Error(L, "%s:SetParent(): Cannot set a 'nil' parent for control points", point->DisplayName());
        return 0;
    }
    const uintptr_t pathType = reinterpret_cast<uintptr_t>(&Anim::s_pathClass.typeId);
    Anim *path = nullptr;
    if (Game::Lua::IsString(L, 2)) {
        // CScriptObject::GetScriptObjectByName(name, Path).
        Game::Lua::PushString(L, Game::Lua::ToString(L, 2));
        Game::Lua::RawGet(L, Game::Lua::GLOBALS_INDEX);
        void *object = Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE ? Game::Lua::ResolveObject(L, -1) : nullptr;
        Game::Lua::SetTop(L, -2);
        if (object != nullptr && IsOurs(object) && Game::Lua::IsA(object, pathType))
            path = static_cast<Anim *>(object);
    } else if (Game::Lua::Type(L, 2) == Game::Lua::TYPE_TABLE) {
        void *object = Game::Lua::ResolveObject(L, 2);
        if (object == nullptr) {
            Game::Lua::Error(L, "%s:SetParent(): Couldn't find 'this' in parent object", point->DisplayName());
            return 0;
        }
        if (!IsOurs(object) || !Game::Lua::IsA(object, pathType)) {
            Game::Lua::Error(L, "%s:SetParent(): Wrong parent object type, expected Path", point->DisplayName());
            return 0;
        }
        path = static_cast<Anim *>(object);
    }
    if (path == nullptr) {
        Game::Lua::Error(L, "%s:SetParent(): Couldn't find Path named '%s'", point->DisplayName(),
                         Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : "");
        return 0;
    }
    const int oldOrder = point->order;
    if (Game::Lua::IsNumber(L, 3)) {
        point->SetOrder(static_cast<int>(Game::Lua::ToNumber(L, 3)) - 1, false, false);
    } else {
        int value = point->order;
        if (value == -1)
            value = path->GetMaxControlPointOrder() + 1;
        point->SetOrder(value, false, true);
    }
    if (!point->ChangeParent(path) && oldOrder != point->order)
        path->NotifyControlPointOrderChanged(point);
    return 0;
}

int __fastcall Script_PointSetOffset(void *L) {
    ControlPoint *point = PointSelf(L);
    if (point == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2) || !Game::Lua::IsNumber(L, 3)) {
        Game::Lua::Error(L, "Usage: %s:SetOffset(x, y)", point->DisplayName());
        return 0;
    }
    point->SetOffset(Game::PixelToInternal(static_cast<float>(Game::Lua::ToNumber(L, 2))),
                     Game::PixelToInternal(static_cast<float>(Game::Lua::ToNumber(L, 3))));
    return 0;
}

int __fastcall Script_PointGetOffset(void *L) {
    ControlPoint *point = PointSelf(L);
    if (point == nullptr)
        return 0;
    Game::Lua::PushNumber(L, Game::InternalToPixel(point->offsetX));
    Game::Lua::PushNumber(L, Game::InternalToPixel(point->offsetY));
    return 2;
}

int __fastcall Script_PointSetOrder(void *L) {
    ControlPoint *point = PointSelf(L);
    if (point == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:SetOrder(order)", point->DisplayName());
        return 0;
    }
    point->SetOrder(static_cast<int>(Game::Lua::ToNumber(L, 2)) - 1, true, false);
    return 0;
}

int __fastcall Script_PointGetOrder(void *L) {
    ControlPoint *point = PointSelf(L);
    if (point == nullptr)
        return 0;
    Game::Lua::PushNumber(L, point->order + 1);
    return 1;
}

const Game::Lua::FrameMethodEntry kPathMethods[] = {
    {"SetCurve", &Script_SetCurve},
    {"GetCurve", &Script_GetCurve},
    {"GetControlPoints", &Script_GetControlPoints},
    {"CreateControlPoint", &Script_CreateControlPoint},
    {"GetMaxOrder", &Script_GetMaxOrder},
};

// CScriptObject's methods, then CSimpleControlPoint's.
const Game::Lua::FrameMethodEntry kPointMethods[] = {
    {"GetObjectType", &Script_GetObjectType}, {"IsObjectType", &Script_IsObjectType},
    {"GetName", &Script_GetName},             {"GetParent", &Script_GetParent},
    {"SetParent", &Script_PointSetParent},    {"SetOffset", &Script_PointSetOffset},
    {"GetOffset", &Script_PointGetOffset},    {"SetOrder", &Script_PointSetOrder},
    {"GetOrder", &Script_PointGetOrder},
};

template <size_t N> constexpr int Count(const Game::Lua::FrameMethodEntry (&)[N]) { return static_cast<int>(N); }

} // namespace

Class Anim::s_pathClass{"Path", &Anim::s_class, nullptr, 0, kPathMethods, Count(kPathMethods), 0, 0};
Class ControlPoint::s_class{"ControlPoint", nullptr, nullptr, 0, kPointMethods, Count(kPointMethods), 0, 0};

} // namespace Animation
