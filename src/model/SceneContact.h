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

// The client mover's contact geometry, transcribed for the scene's own
// bodies: the volume a unit occupies, the facets of the world around a move,
// and the first contact of the moving volume with them (0x00631440 through
// 0x00632F80). Each function is one client function, with its arithmetic in
// the client's order and precision (53-bit sums, float stores), so a body
// meets a face where the client's mover would. Nothing here calls the engine:
// the facets come in through Facets::query, and the client's own mover state
// (CMovement, the lists at 0x00C4E52C) is never touched.

#include <cstdint>
#include <vector>

namespace Model::Scene::Contact {

// A plane: a point q is inside when n·q + d <= 0.
struct Plane {
    float n[3];
    float d;
};

// A world facet, the 52-byte record of the facet emitter (0x00671CC0): its
// plane, then its three vertices.
struct Facet {
    Plane plane;
    float v[3][3];
};
static_assert(sizeof(Facet) == 0x34, "the client's facet record");

// The facet the emitter writes for three vertices: normal (v1 - v0) × (v2 -
// v0) normalized, d = -n·v0.
Facet MakeFacet(const float (&v0)[3], const float (&v1)[3], const float (&v2)[3]);

// IsPointOverListedTriangle (0x006335D0): whether p lies at most 1/12
// outside each edge of the facet, the edges extruded straight up, so above
// or below the facet in plan.
bool PointOverFacet(const Facet &facet, const float (&p)[3]);

// The facet lists a sweep reads and the box they cover: the solid list
// (0x00C4E52C), the liquid list a swimmer adds (0x00C4E564), and their cached
// box (0x00C4E5A0, min then max). `query` is the world query (0x006721B0)
// with the mover's class: it empties `solid` and fills it with the facets in
// a box, false when the query fails. `liquidQuery` is the same query with
// the swimmer's class 0x30000, water and ocean surfaces, into `liquid`.
struct Facets {
    std::vector<Facet> solid;
    std::vector<Facet> liquid;
    float box[6];
    bool (*query)(void *context, const float (&box)[6], std::vector<Facet> &out);
    bool (*liquidQuery)(void *context, const float (&box)[6], std::vector<Facet> &out);
    void *context;
};

// The swimmer's liquid query (0x00633E90, 0x006320E0): `liquidQuery` over the
// box, its return not read, then every liquid plane reversed (0x00637330
// and FCHS), so a surface faces down and meets a rising swimmer as a ceiling.
void QueryLiquid(Facets &facets, const float (&box)[6]);

// The volume's base box, as the substep's query frame (0x00633840) starts
// it: (p.x - r, p.y - r, p.z) to (p.x + r, p.y + r, p.z + H).
void BaseBox(const float (&p)[3], float radius, float height, float (&box)[6]);

// What a sweep met: the travel to the first contact, the facet found (an
// index into the list swept, its size when none), and the leading faces of
// the volume that touch within 1/720 of it.
struct Contact {
    float distance;
    uint32_t hit;
    uint32_t count;
    Plane faces[9];
};

// CMovement::SweepVolume (0x00632BA0): the volume of a body (a box of
// half-width `radius` up to `height`, over a square pyramid whose apex is
// `p`) swept along the unit vector `dir` for `length`, against the solid
// list or with `liquid` the liquid list. A `swimmer` (flag 0x200000) gathers
// the liquid list too, and its volume against that list is three quarters of
// `height` tall. The hover's branch is not taken: the scene's bodies do not
// hover. False when the facet query fails.
bool Sweep(Facets &facets, bool liquid, bool swimmer, const float (&p)[3], float radius, float height,
           const float (&dir)[3], float length, Contact &out);

} // namespace Model::Scene::Contact
