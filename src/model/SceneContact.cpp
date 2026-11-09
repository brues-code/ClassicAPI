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

// The client mover's contact sweep, function by function (see SceneContact.h).
//
// Precision: the client runs these on the x87 at 53 bits and stores floats,
// so each sum here is a double and each store a float cast, in the operand
// order of the disassembly (benilla-re systems/collision/contact.md has the
// instruction-level notes; its crates/collision/src/resolve_clip.rs is the
// difftested transcription this follows). Where that transcription and the
// binary differ, the binary wins: 0x00632BA0 starts the best distance and
// each face's cap at the length asked for, not at the 1/36 floor
// (0x00632BC3, 0x00632E10); the degenerate-edge test of the prisms returns
// only below 2^-20 (0x006325B5, 0x006330C6 `jnp`); 0x006329E0 divides at the
// parallel bound; and the quad prism tests its side against the vertex
// before the edge with the sum (z, x, y) (0x006330EA, 0x00633126).

#include "model/SceneContact.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <xmmintrin.h>

namespace Model::Scene::Contact {

namespace {

float Bits(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

double D(float x) { return x; }

const float kSkin = Bits(0x3AB60B61);           // 1/720 (0x0080DFEC)
const float kNegSkin = Bits(0xBAB60B61);        // -1/720 (0x0080DFF0)
const float kAllBehind = Bits(0xB5800000);      // -2^-20 (0x0080DFF4)
const float kDegenerate = Bits(0x35800000);     // 2^-20 (0x008026BC)
const float kParallel = Bits(0x34800000);       // 2^-22 (0x008029D4)
const float kThirtySixth = Bits(0x3CE38E39);    // 1/36 (0x007FF9D0)
const float kNegThirtySixth = Bits(0xBCE38E39); // -1/36 (0x007FF9C8)
const float kFacing = Bits(0xB727C5AC);         // -1e-5 (0x0080C5C4)
const float kGrowth = Bits(0x3E2AAAAB);         // 1/6, the gather's growth
const float kBevelOut = Bits(0x3F613036);       // sin 61.6° (0x0080DFE4)
const float kBevelDown = Bits(0x3EF386A4);      // cos 61.6° (0x0080DFE0)
const float kWaist = Bits(0x3FECB91B);          // tan 61.6° (0x0080C740)
const float kFltMax = Bits(0x7F7FFFFF);
const float kTwelfth = Bits(0x3DAAAAAB);        // 1/12, the point-over-facet margin

// The volume's faces as vertex indices (0x00631BE0's store block): the five
// box faces as quads, in plane order, then the four bevels as triangles.
constexpr uint8_t kFaces[32] = {1, 2, 6, 5, 3, 4, 8, 7, 2, 3, 7, 6, 4, 1, 5, 8,
                                5, 6, 7, 8, 0, 1, 2, 0, 3, 4, 0, 2, 3, 0, 4, 1};

// A clip polygon (0xF4 bytes): up to 15 vertices, the plane each one came
// from, and the count.
struct Polygon {
    float v[15][3];
    int32_t tag[15];
    uint32_t count;
};

struct Volume {
    Plane planes[9];
    float v[9][3];
};

// ClipPolygonByPlane (0x006318C0): keeps the inside of a plane, with a
// 1/720 band, tagging the vertices it adds.
void ClipByPlane(Polygon &poly, const Plane &pl, int32_t tag) {
    const uint32_t n = poly.count;
    if (n == 0)
        return;
    float s[15];
    float most = -kFltMax, least = kFltMax;
    for (uint32_t i = 0; i < n; ++i) {
        const float *v = poly.v[i];
        s[i] = static_cast<float>(-(((D(v[0]) * pl.n[0] + D(v[2]) * pl.n[2]) + D(v[1]) * pl.n[1]) + pl.d));
        if (s[i] > most)
            most = s[i];
        if (s[i] < least)
            least = s[i];
    }
    if (least > kNegSkin)
        return;
    if (most < kSkin) {
        poly.count = 0;
        return;
    }
    const Polygon old = poly;
    uint32_t out = 0;
    const auto keep = [&](uint32_t i) {
        std::memcpy(poly.v[out], old.v[i], sizeof poly.v[out]);
        poly.tag[out++] = old.tag[i];
    };
    uint32_t prev = n - 1;
    for (uint32_t cur = 0; cur < n && out + 2 <= 15; prev = cur++) {
        const float sp = s[prev], sc = s[cur];
        const bool prevIn = !(sp < 0.0f), curIn = !(sc < 0.0f);
        const float *p = old.v[prev], *c = old.v[cur];
        if (!prevIn && curIn) {
            if (sc > kSkin) {
                // Out to in (0x00631A36): z's difference rounds first, y's
                // product stays unrounded.
                const float t = static_cast<float>(D(sp) / (D(sc) - sp));
                const float ex = static_cast<float>((D(c[0]) - p[0]) * t);
                const double ey = (D(c[1]) - p[1]) * t;
                const float dz = static_cast<float>(D(c[2]) - p[2]);
                const double ez = D(dz) * t;
                poly.v[out][0] = static_cast<float>(D(p[0]) - ex);
                poly.v[out][1] = static_cast<float>(D(p[1]) - ey);
                poly.v[out][2] = static_cast<float>(D(p[2]) - ez);
                poly.tag[out++] = tag;
            }
            keep(cur);
        } else if (prevIn && curIn) {
            keep(cur);
        } else if (prevIn && !curIn && sp > kSkin) {
            // In to out (0x00631B6B): each product rounds.
            const float t = static_cast<float>(D(sp) / (D(sc) - sp));
            for (int k = 0; k < 3; ++k)
                poly.v[out][k] = static_cast<float>(D(p[k]) - static_cast<float>((D(c[k]) - p[k]) * t));
            poly.tag[out++] = tag;
        }
    }
    poly.count = out < 3 ? 0 : out;
}

// ClipPolygonByPlanes (0x00631870): by each plane in turn, false once empty.
bool ClipByPlanes(Polygon &poly, const Plane *planes, int count) {
    for (int i = 0; i < count; ++i) {
        ClipByPlane(poly, planes[i], i);
        if (poly.count == 0)
            return false;
    }
    return true;
}

// ClipPolygonByPlanesExcept (0x00632980): by every volume plane but one.
bool ClipByPlanesExcept(Polygon &poly, const Plane (&planes)[9], int skip) {
    for (int i = 0; i < 9; ++i) {
        if (i == skip)
            continue;
        ClipByPlane(poly, planes[i], i);
        if (poly.count == 0)
            return false;
    }
    return true;
}

// PlaneFromTriangle (0x00637480): the normalized plane through three points,
// normal (p1 - p0) × (p2 - p0).
Plane PlaneFromPoints(const float (&p0)[3], const float (&p1)[3], const float (&p2)[3]) {
    const double a[3] = {D(p2[0]) - p0[0], D(p2[1]) - p0[1], D(p2[2]) - p0[2]};
    const float bx = static_cast<float>(D(p1[0]) - p0[0]);
    const float by = static_cast<float>(D(p1[1]) - p0[1]);
    const double bz = D(p1[2]) - p0[2];
    const float n[3] = {static_cast<float>(D(by) * a[2] - bz * a[1]), static_cast<float>(bz * a[0] - a[2] * bx),
                        static_cast<float>(a[1] * bx - D(by) * a[0])};
    const double inverse = 1.0 / std::sqrt((D(n[0]) * n[0] + D(n[1]) * n[1]) + D(n[2]) * n[2]);
    const double f[3] = {D(n[0]) * inverse, D(n[1]) * inverse, D(n[2]) * inverse};
    Plane pl;
    for (int k = 0; k < 3; ++k)
        pl.n[k] = static_cast<float>(f[k]);
    pl.d = static_cast<float>(-((f[2] * p0[2] + f[1] * p0[1]) + f[0] * p0[0]));
    return pl;
}

// One edge plane of a face swept along `s` (0x00632460 / 0x00632F80): the
// plane through the edge a→b and a + s, turned so that `other`, a face
// vertex off the edge, is not on its positive side. False for an edge too
// short or parallel to `s`.
bool EdgePlane(const float (&a)[3], const float (&b)[3], const float (&other)[3], const float (&s)[3], bool quad,
               Plane &out) {
    float moved[3], u[3], w[3];
    for (int k = 0; k < 3; ++k)
        moved[k] = static_cast<float>(D(a[k]) + s[k]);
    for (int k = 0; k < 3; ++k) {
        u[k] = static_cast<float>(D(moved[k]) - a[k]);
        w[k] = static_cast<float>(D(b[k]) - a[k]);
    }
    // w × u (0x0063256F: three `fsubp st(1),st`).
    const float cz = static_cast<float>(D(u[1]) * w[0] - D(w[1]) * u[0]);
    const float cy = static_cast<float>(D(w[2]) * u[0] - D(u[2]) * w[0]);
    const float cx = static_cast<float>(D(u[2]) * w[1] - D(u[1]) * w[2]);
    if (std::fabs((D(cy) * cy + D(cz) * cz) + D(cx) * cx) < kDegenerate)
        return false;
    out = PlaneFromPoints(a, b, moved);
    float t[3];
    for (int k = 0; k < 3; ++k)
        t[k] = static_cast<float>(D(other[k]) - a[k]);
    const double side = quad ? (D(t[2]) * out.n[2] + D(t[0]) * out.n[0]) + D(t[1]) * out.n[1]
                             : (D(t[2]) * out.n[2] + D(t[1]) * out.n[1]) + D(t[0]) * out.n[0];
    if (side > 0.0) {
        for (float &c : out.n)
            c = -c;
        out.d = -out.d;
    }
    return true;
}

// BuildTrianglePrism (0x00632460): the edge planes of a bevel swept along s.
bool TrianglePrism(const Volume &volume, const uint8_t *index, const float (&s)[3], Plane (&out)[4]) {
    for (int i = 0; i < 3; ++i)
        if (!EdgePlane(volume.v[index[i]], volume.v[index[(i + 1) % 3]], volume.v[index[(i + 2) % 3]], s, false,
                       out[i]))
            return false;
    return true;
}

// BuildQuadPrism (0x00632F80): the edge planes of a box face swept along s.
bool QuadPrism(const Volume &volume, const uint8_t *index, const float (&s)[3], Plane (&out)[4]) {
    for (int i = 0; i < 4; ++i)
        if (!EdgePlane(volume.v[index[i]], volume.v[index[(i + 1) & 3]], volume.v[index[(i + 3) & 3]], s, true,
                       out[i]))
            return false;
    return true;
}

// BuildVolumePlanes (0x00631440) and CMovement::BuildVolume (0x00631BE0):
// the nine planes and nine vertices of the volume at p.
Volume BuildVolume(const float (&p)[3], float radius, float height) {
    const double x = p[0], y = p[1], z = p[2], r = radius, h = height;
    const double a = kBevelOut, b = kBevelDown, zero = 0.0;
    Volume v{};
    const auto set = [&](int i, float nx, float ny, float nz, double d) {
        v.planes[i] = Plane{{nx, ny, nz}, static_cast<float>(d)};
    };
    set(0, -1.0f, 0.0f, 0.0f, x - r);
    set(1, 1.0f, 0.0f, 0.0f, -x - r);
    set(2, 0.0f, 1.0f, 0.0f, -y - r);
    set(3, 0.0f, -1.0f, 0.0f, y - r);
    set(4, 0.0f, 0.0f, 1.0f, -z - h);
    set(5, -kBevelOut, 0.0f, -kBevelDown, (x * a + z * b) + y * zero);
    set(6, kBevelOut, 0.0f, -kBevelDown, (z * b - x * a) + y * zero);
    set(7, 0.0f, kBevelOut, -kBevelDown, (x * zero + z * b) - y * a);
    set(8, 0.0f, -kBevelOut, -kBevelDown, (x * zero + z * b) + y * a);

    const float waist = static_cast<float>(z + r * kWaist);
    const float top = static_cast<float>(z + h);
    const float lowX = static_cast<float>(x - r), highX = static_cast<float>(x + r);
    const float lowY = static_cast<float>(y - r), highY = static_cast<float>(y + r);
    const float corners[4][2] = {{lowX, lowY}, {lowX, highY}, {highX, highY}, {highX, lowY}};
    std::memcpy(v.v[0], p, sizeof v.v[0]);
    for (int i = 0; i < 4; ++i) {
        v.v[1 + i][0] = v.v[5 + i][0] = corners[i][0];
        v.v[1 + i][1] = v.v[5 + i][1] = corners[i][1];
        v.v[1 + i][2] = waist;
        v.v[5 + i][2] = top;
    }
    return v;
}

// PlaneDistanceAlongRay (0x006329E0): how far a plane moves along `dir`
// before it reaches q; n·q + d undivided when it runs parallel.
double DistanceAlongRay(const float (&q)[3], const Plane &pl, const float (&dir)[3]) {
    const double num = ((D(q[2]) * pl.n[2] + D(q[1]) * pl.n[1]) + D(q[0]) * pl.n[0]) + pl.d;
    const double den = (D(pl.n[2]) * dir[2] + D(pl.n[1]) * dir[1]) + D(pl.n[0]) * dir[0];
    return std::fabs(den) < kParallel ? num : num / den;
}

// PolygonContactDistance (0x00632830): the travel until volume face k
// reaches the polygon, 0 when it already touches; nothing when the polygon
// lies wholly behind the face, past 1/36 of the volume's inside.
bool ContactDistance(Polygon &poly, const float (&dir)[3], const Plane (&planes)[9], int k, float &best) {
    float m = best;
    bool behind = true;
    for (uint32_t i = 0; i < poly.count; ++i) {
        const double t = DistanceAlongRay(poly.v[i], planes[k], dir);
        if (t < m)
            m = t > 0.0 ? static_cast<float>(t) : 0.0f;
        if (t > kAllBehind)
            behind = false;
    }
    if (behind) {
        if (!ClipByPlanesExcept(poly, planes, k))
            return false;
        for (uint32_t i = 0; i < poly.count; ++i)
            if (DistanceAlongRay(poly.v[i], planes[k], dir) > kNegThirtySixth)
                behind = false;
        if (behind)
            return false;
    }
    if (!(m >= best)) {
        best = m;
        return true;
    }
    return false;
}

// FindNearestFacet (0x00632700): of the facets facing against the move, the
// one volume face k reaches first inside its swept prism.
bool NearestFacet(const std::vector<Facet> &list, const float (&dir)[3], const Plane *prism, int prismCount,
                  const Plane (&planes)[9], int k, float &best, uint32_t &hit) {
    bool found = false;
    for (uint32_t i = 0; i < list.size(); ++i) {
        const Facet &f = list[i];
        const double facing = (D(f.plane.n[2]) * dir[2] + D(f.plane.n[1]) * dir[1]) + D(f.plane.n[0]) * dir[0];
        if (facing > kFacing)
            continue;
        Polygon poly;
        std::memcpy(poly.v, f.v, sizeof f.v);
        poly.tag[0] = poly.tag[1] = poly.tag[2] = -1;
        poly.count = 3;
        if (!ClipByPlanes(poly, prism, prismCount))
            continue;
        float t = kFltMax;
        if (!ContactDistance(poly, dir, planes, k, t))
            continue;
        if (t <= best) {
            best = t;
            hit = i;
            found = true;
        }
    }
    return found;
}

// The band rule of 0x00632280 and 0x00632BA0: a face nearer by more than
// 1/720 replaces the list, one within 1/720 joins it, and the nearest leads.
void Band(const Plane &face, float t, Contact &out, float &best) {
    if (D(best) - kSkin > t) {
        out.faces[0] = face;
        out.count = 1;
    } else if (D(best) + kSkin > t && out.count < 9) {
        out.faces[out.count++] = face;
    }
    if (t < best) {
        best = t;
        if (out.count > 1)
            std::swap(out.faces[out.count - 1], out.faces[0]);
    }
}

// A leading face: its normal along the sweep, or NaN.
bool Leading(double dot) { return dot > 0.0 || std::isnan(dot); }

// CMovement::FindBevelContacts (0x00632280): the four bevels.
void Bevels(const std::vector<Facet> &list, const float (&dir)[3], float length, const float (&s)[3],
            const Volume &volume, Contact &out, float &best) {
    for (int b = 0; b < 4; ++b) {
        const Plane &face = volume.planes[5 + b];
        if (!Leading((D(face.n[2]) * s[2] + D(face.n[0]) * s[0]) + D(face.n[1]) * s[1]))
            continue;
        Plane prism[4];
        if (!TrianglePrism(volume, kFaces + 20 + 3 * b, s, prism))
            continue;
        float t = length;
        if (!NearestFacet(list, dir, prism, 3, volume.planes, 5 + b, t, out.hit))
            continue;
        Band(face, t, out, best);
    }
}

bool InBox(const float (&box)[6], const float (&p)[3]) {
    for (int i = 0; i < 3; ++i)
        if (box[i] > p[i] || p[i] > box[i + 3])
            return false;
    return true;
}

// CMovement::GatherSweepFacets (0x00631E70): the facets around the volume
// moved by s, unless the cached box already holds it.
bool Gather(Facets &facets, const float (&p)[3], float radius, float height, const float (&s)[3]) {
    float e[3];
    for (int k = 0; k < 3; ++k)
        e[k] = static_cast<float>(D(p[k]) + s[k]);
    float low[3] = {static_cast<float>(D(e[0]) - radius), static_cast<float>(D(e[1]) - radius), e[2]};
    float high[3] = {static_cast<float>(D(e[0]) + radius), static_cast<float>(D(e[1]) + radius),
                     static_cast<float>(D(e[2]) + height)};
    if (InBox(facets.box, low) && InBox(facets.box, high))
        return true;
    float query[6];
    for (int k = 0; k < 3; ++k) {
        const float grownLow = static_cast<float>(D(low[k]) - kGrowth);
        const float grownHigh = static_cast<float>(D(high[k]) + kGrowth);
        // UnionBoxes (0x006373B0): the cached box on ties and NaN.
        query[k] = grownLow > facets.box[k] ? facets.box[k] : grownLow;
        query[k + 3] = facets.box[k + 3] > grownHigh ? facets.box[k + 3] : grownHigh;
    }
    if (!facets.query(facets.context, query, facets.solid))
        return false;
    std::memcpy(facets.box, query, sizeof query);
    return true;
}

} // namespace

Facet MakeFacet(const float (&v0)[3], const float (&v1)[3], const float (&v2)[3]) {
    // FlushTriangleBatchFacets (0x00671CC0), its SSE path: the client sets
    // s_useSse from the CPU's SSE bit (CMap::Initialize, 0x006920A7), so
    // every machine that runs it today scales by rsqrtss, a 12-bit estimate.
    Facet f;
    std::memcpy(f.v[0], v0, sizeof f.v[0]);
    std::memcpy(f.v[1], v1, sizeof f.v[1]);
    std::memcpy(f.v[2], v2, sizeof f.v[2]);
    const double ax = D(v2[0]) - v0[0], ay = D(v2[1]) - v0[1], az = D(v2[2]) - v0[2];
    const float bx = static_cast<float>(D(v1[0]) - v0[0]);
    const float by = static_cast<float>(D(v1[1]) - v0[1]);
    const double bz = D(v1[2]) - v0[2];
    const float nx = static_cast<float>(D(by) * az - bz * ay);
    const float ny = static_cast<float>(bz * ax - az * bx);
    const float nz = static_cast<float>(ay * bx - D(by) * ax);
    const float length2 = static_cast<float>((D(nx) * nx + D(ny) * ny) + D(nz) * nz);
    const float k = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(length2)));
    const double x = D(k) * nx, y = D(k) * ny, z = D(k) * nz;
    f.plane.n[0] = static_cast<float>(x);
    f.plane.n[1] = static_cast<float>(y);
    f.plane.n[2] = static_cast<float>(z);
    f.plane.d = static_cast<float>(-((z * v0[2] + y * v0[1]) + x * v0[0]));
    return f;
}

bool PointOverFacet(const Facet &facet, const float (&p)[3]) {
    // 0x0063364A: the facet's prism along (0, 0, 1), its planes starting
    // as (0, 0, 1, 0); 0x00633671 sums (x, z, y) and fails above 1/12.
    const float up[3] = {0.0f, 0.0f, 1.0f};
    Plane planes[3] = {{{0.0f, 0.0f, 1.0f}, 0.0f}, {{0.0f, 0.0f, 1.0f}, 0.0f}, {{0.0f, 0.0f, 1.0f}, 0.0f}};
    for (int i = 0; i < 3; ++i)
        if (!EdgePlane(facet.v[i], facet.v[(i + 1) % 3], facet.v[(i + 2) % 3], up, false, planes[i]))
            break;
    for (const Plane &pl : planes)
        if (((D(pl.n[0]) * p[0] + D(pl.n[2]) * p[2]) + D(pl.n[1]) * p[1]) + pl.d > kTwelfth)
            return false;
    return true;
}

void BaseBox(const float (&p)[3], float radius, float height, float (&box)[6]) {
    box[0] = static_cast<float>(D(p[0]) - radius);
    box[1] = static_cast<float>(D(p[1]) - radius);
    box[2] = p[2];
    box[3] = static_cast<float>(D(p[0]) + radius);
    box[4] = static_cast<float>(D(p[1]) + radius);
    box[5] = static_cast<float>(D(p[2]) + height);
}

bool Sweep(Facets &facets, const float (&p)[3], float radius, float height, const float (&dir)[3], float length,
           Contact &out) {
    out.count = 0;
    if (std::fabs(length) < kDegenerate) {
        out.distance = 0.0f;
        out.hit = static_cast<uint32_t>(facets.solid.size());
        return true;
    }
    // At least 1/36 of sweep, but every distance is capped at the length.
    const float d = length > kThirtySixth ? length : kThirtySixth;
    const float s[3] = {static_cast<float>(D(d) * dir[0]), static_cast<float>(D(d) * dir[1]),
                        static_cast<float>(D(d) * dir[2])};
    const Volume volume = BuildVolume(p, radius, height);
    if (!Gather(facets, p, radius, height, s)) {
        out.distance = 0.0f;
        return false;
    }
    out.hit = static_cast<uint32_t>(facets.solid.size());
    float best = length;
    Bevels(facets.solid, dir, length, s, volume, out, best);
    for (int a = 0; a < 5; ++a) {
        const Plane &face = volume.planes[a];
        if (!Leading((D(s[2]) * face.n[2] + D(s[1]) * face.n[1]) + D(s[0]) * face.n[0]))
            continue;
        Plane prism[4];
        if (!QuadPrism(volume, kFaces + 4 * a, s, prism))
            continue;
        float t = length;
        if (!NearestFacet(facets.solid, dir, prism, 4, volume.planes, a, t, out.hit))
            continue;
        Band(face, t, out, best);
    }
    out.distance = best < kSkin ? 0.0f : best;
    return true;
}

} // namespace Model::Scene::Contact
