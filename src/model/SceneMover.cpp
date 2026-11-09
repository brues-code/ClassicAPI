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

// The client's mover for a scene body (see SceneMover.h), one client function
// per function here. As in SceneContact.cpp, every sum is a double and every
// store a float cast, in the operand order of the disassembly. The sources
// are benilla-re's systems/collision docs (instruction-level notes) and its
// crates/collision transcription; where they differ, the binary was read and
// wins: the fall-away lookahead starts a real fall and sweeps (+0x68, +0x6C,
// -D) (0x00635FF7, 0x00636014), and the airborne push carries over to later
// steps (airborne.md, Stall rules).
//
// The layers, top down: the tick (0x00616800, 0x00616620) cuts the frame
// into a window, the integrator (0x00616DE0, 0x00616CB0) turns the time
// since the anchors into a displacement, the substep loop (0x00634040)
// gathers the facets around each substep (0x00633840) and runs the walk
// (0x006367B0), fall (0x00635B00) or swimmer's free-move (0x00634640)
// resolver, and those sweep the body's volume (SceneContact.h) and deflect,
// step and settle. After a tick the unit side decides whether the body swims
// (UpdateSwimState).

#include "model/SceneMover.h"

#include <cmath>
#include <cstring>

namespace Model::Scene::Mover {

namespace {

float Bits(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

double D(float x) { return x; }

const float kMsToSec = Bits(0x3A83126F);      // 0.001 (0x00801360), also the 0.001 nudge
const float kThousand = Bits(0x447A0000);     // 1000.0 (0x008022CC)
const float kHalf = Bits(0x3F000000);         // 0.5 (0x007FFA24)
const float kOne = Bits(0x3F800000);          // 1.0 (0x007FF9D8)
const float kTiny = Bits(0x35800000);         // 2^-20 (0x008026BC)
const float kParallel = Bits(0x34800000);     // 2^-22 (0x008029D4)
const float kSkin = Bits(0x3AB60B61);         // 1/720 (0x0080DFEC)
const float kThirtySixth = Bits(0x3CE38E39);  // 1/36 (0x007FF9D0)
const float kNinth = Bits(0x3DE38E39);        // 1/9 (0x0080DFF8)
const float kDownBias = Bits(0x3C23D70A);     // 0.01 (0x008029D0)
const float kStall = Bits(0x3A03126F);        // 0.0005 (0x0080C698)
const float kStallReach = Bits(0x3F97E4AD);   // 1.1866661 (0x0080E018)
const float kTenth = Bits(0x3DCCCCCD);        // 0.1 (0x00801620)
const float kCos10 = Bits(0x3F7C1C5C);        // cos 10° (0x0080E01C)
const float kCos50 = Bits(0x3F248DBB);        // cos 50° (0x0080DFFC), the floor limit
const float kTan50 = Bits(0x3F988B62);        // tan 50° (0x0080E008)
const float kNegTan50 = Bits(0xBF988B62);     // -tan 50° (0x0080E010)
const float kSqrt2 = Bits(0x3FB504F3);        // √2 (0x0080E00C)
const float kRootHalf = Bits(0x3F3504F3);     // √½, the diagonal
const float kWaist = Bits(0x3FECB91B);        // tan 61.6° (0x0080C740)
const float kBevelZ = Bits(0xBEF386A4);       // the bevels' normal z (0x0080E014)
const float kBisect = Bits(0x3F237868);       // the two-bevel normalizer (0x0080E024)
const float kG = Bits(0x419A542F);            // g (0x0081DA58)
const float kHalfG = Bits(0x411A542F);        // g/2 (0x0081DA60)
const float kTwoG = Bits(0x421A542F);         // 2g (0x0081DA64)
const float kInvG = Bits(0x3D54536A);         // 1/g (0x0080E020)
const float kNegInvG = Bits(0xBD54536A);      // -1/g (0x0081DA5C)
const float kTwoOverG = Bits(0x3DD4536A);     // 2/g (0x0081DA68)
const float kTerminal = Bits(0x4270978E);     // the fall's cap (0x0087D894)
const float kTerminalSlow = Bits(0x40E00000); // the cap under 0x20000000 (0x0087D898)
const float kJumpSpeed = Bits(0xC0FE93D8);    // the jump's launch (0x007C626F)
const float kSwimJumpSpeed = Bits(0xC1118C48);
const float kTwoPi = Bits(0x40C90FDB);        // 0x00CF5D00
const float kHalfPi = Bits(0x3FC90FDB);       // pi/2 (0x00808ACC), the pitch clamp
const float kNegHalfPi = Bits(0xBFC90FDB);    // -pi/2 (0x0080C5E4)
const float kThreeQuarters = Bits(0x3F400000); // 0.75 (0x008012CC)
const float kFltMax = Bits(0x7F7FFFFF);
const float kNegFltMax = Bits(0xFF7FFFFF);
const float kCellCentre = Bits(0x46855555);   // 17066.666 (0x00616BF0)
const float kCellSpan = Bits(0x47055355);     // 34131.332 (0x0080C738)
const float kDefaultRadius = Bits(0x3EAAAAAB);
const float kDefaultHeight = Bits(0x4001C71C);

// The speeds a server's create block gives a character (the block's wire
// +0x50 to +0x64, copied by 0x00619320): vmangos' baseMoveSpeed.
constexpr float kWalkSpeed = 2.5f;
constexpr float kRunSpeed = 7.0f;
constexpr float kRunBackSpeed = 4.5f;
constexpr float kSwimSpeed = 4.722222f;
constexpr float kSwimBackSpeed = 2.5f;
constexpr float kTurnRate = 3.141594f;

constexpr uint32_t kMaxWindowMs = 250; // 0x00616642
// A bound on the substep passes, which the client does not have: each of
// its passes consumes time or changes the flags, so it never loops for
// long; this keeps a fault in the port from hanging the game.
constexpr int kMaxPasses = 64;

// __ftol after the half added away from zero (0x006359AE, 0x00635FAF).
// A NaN or out-of-range value converts to 0x8000000000000000, whose low
// dword is 0.
int32_t Round(double x) {
    const double biased = x > 0.0 ? x + kHalf : x - kHalf;
    if (!(biased > -9.2e18 && biased < 9.2e18))
        return 0;
    return static_cast<int32_t>(static_cast<int64_t>(biased));
}

// DotVector3 (0x00602630).
double Dot(const float (&a)[3], const float (&b)[3]) {
    return (D(a[2]) * b[2] + D(a[1]) * b[1]) + D(a[0]) * b[0];
}

using Contact::Facet;
using Contact::Plane;

// One sweep (0x00632BA0) of the body's volume from where it stands, against
// the solid list or with `liquid` the liquid list.
struct Hit {
    bool ok;
    float distance;
    uint32_t index;
    uint32_t count;
    Plane faces[9];
};

Hit Sweep(Body &b, const float (&dir)[3], float length, bool liquid = false) {
    Contact::Contact c{};
    Hit h{};
    h.ok = Contact::Sweep(b.facets, liquid, (b.flags & FLAG_SWIMMING) != 0, b.position, b.radius, b.height, dir,
                          length, c);
    h.distance = c.distance;
    h.index = c.hit;
    h.count = c.count;
    std::memcpy(h.faces, c.faces, sizeof h.faces);
    return h;
}

uint32_t FacetCount(const Body &b) { return static_cast<uint32_t>(b.facets.solid.size()); }

// ---- kinematics (0x007C4850 to 0x007C5CD0) ------------------------------------------

// The current speed (0x007C4C90).
float SelectSpeed(const Body &b, bool walk) {
    if ((b.flags & 0xF) == 0)
        return 0.0f;
    if (b.flags & FLAG_SWIMMING) {
        if (b.flags & FLAG_BACKWARD)
            return b.swimBackSpeed <= b.swimSpeed ? b.swimBackSpeed : b.swimSpeed;
        return b.swimSpeed;
    }
    if ((b.flags & 0x100) || walk)
        return b.runSpeed > b.walkSpeed ? b.walkSpeed : b.runSpeed;
    if (b.flags & FLAG_BACKWARD)
        return b.runBackSpeed <= b.runSpeed ? b.runBackSpeed : b.runSpeed;
    return b.runSpeed;
}

// 0x007C5C20.
void RefreshSpeed(Body &b, bool force) {
    if ((b.flags & FLAG_FALLING) && !force)
        return;
    b.speed = SelectSpeed(b, force);
}

// The turn rate (0x007C5C50), unrounded.
double TurnRate(const Body &b) {
    double rate = 0.0;
    if (b.flags & FLAG_TURN_LEFT)
        rate = b.turnRate;
    else if (b.flags & FLAG_TURN_RIGHT)
        rate = -D(b.turnRate);
    if (b.flags & 0x200F)
        rate *= 0.75;
    return rate;
}

// The pitch rate (0x007C5C90), unrounded: up for 0x40, down for 0x80.
double PitchRate(const Body &b) {
    double rate = 0.0;
    if (b.flags & FLAG_PITCH_UP)
        rate = b.turnRate;
    else if (b.flags & FLAG_PITCH_DOWN)
        rate = -D(b.turnRate);
    if (b.flags & 0x200F)
        rate *= kThreeQuarters;
    return rate;
}

// The heading basis (0x007C5880).
void Basis(Body &b) {
    const double f = b.anchorFacing;
    b.heading[0] = static_cast<float>(std::cos(f));
    b.heading[1] = static_cast<float>(std::sin(f));
    const float p = b.anchorPitch;
    if ((b.flags & FLAG_SWIMMING) && !(std::fabs(p) < kTiny)) {
        b.pitchBasis[0] = static_cast<float>(std::cos(D(p)));
        b.pitchBasis[1] = static_cast<float>(std::sin(D(p)));
        b.direction[0] = static_cast<float>(D(b.heading[0]) * b.pitchBasis[0]);
        b.direction[1] = static_cast<float>(D(b.heading[1]) * b.pitchBasis[0]);
        b.direction[2] = b.pitchBasis[1];
    } else {
        b.direction[0] = b.heading[0];
        b.direction[1] = b.heading[1];
        b.direction[2] = 0.0f;
        b.pitchBasis[0] = 1.0f;
        b.pitchBasis[1] = 0.0f;
    }
}

// The direction from the held bits (0x007C5A20).
void Direction(Body &b, bool force) {
    if ((b.flags & FLAG_FALLING) && !force)
        return;
    Basis(b);
    const float hx = b.heading[0], hy = b.heading[1];
    float side[2] = {0.0f, 0.0f};
    if (b.flags & FLAG_STRAFE_LEFT) {
        side[0] = -hy;
        side[1] = hx;
    } else if (b.flags & FLAG_STRAFE_RIGHT) {
        side[0] = hy;
        side[1] = -hx;
    }
    if ((b.flags & 0x3) && (b.flags & 0xC)) {
        const float k = (b.flags & FLAG_BACKWARD) ? -1.0f : 1.0f;
        const float d[3] = {b.direction[0] * k, b.direction[1] * k, b.direction[2] * k};
        const float h[2] = {hx * k, hy * k};
        const float sum[5] = {static_cast<float>(D(side[0]) + d[0]), static_cast<float>(D(d[1]) + side[1]), d[2],
                              static_cast<float>(D(h[0]) + side[0]), static_cast<float>(D(h[1]) + side[1])};
        b.direction[0] = static_cast<float>(D(sum[0]) * kRootHalf);
        b.direction[1] = static_cast<float>(D(sum[1]) * kRootHalf);
        b.direction[2] = static_cast<float>(D(sum[2]) * kRootHalf);
        b.heading[0] = static_cast<float>(D(sum[3]) * kRootHalf);
        b.heading[1] = static_cast<float>(D(sum[4]) * kRootHalf);
    } else if (b.flags & 0x3) {
        if (b.flags & FLAG_BACKWARD) {
            for (float &c : b.direction)
                c = -c;
            b.heading[0] = -hx;
            b.heading[1] = -hy;
        }
    } else if (b.flags & 0xC) {
        b.direction[0] = side[0];
        b.direction[1] = side[1];
        b.direction[2] = 0.0f;
        b.heading[0] = side[0];
        b.heading[1] = side[1];
    }
}

// CMovement::UpdateAnchors (0x007C5CD0).
void Anchor(Body &b, bool force) {
    std::memcpy(b.anchor, b.position, sizeof b.anchor);
    b.anchorFacing = b.facing;
    b.anchorPitch = b.pitch;
    b.elapsed = 0;
    Direction(b, force);
}

// The facing at t seconds (0x007C4F30).
void IntegrateFacing(Body &b, float t) {
    const float omega = static_cast<float>(TurnRate(b));
    const double r = std::fmod(D(omega) * t + b.anchorFacing, D(kTwoPi));
    b.facing = static_cast<float>(r < 0.0 ? r + kTwoPi : r);
}

// The pitch at t seconds (0x007C4F80), held to +-pi/2; NaN holds at pi/2.
void IntegratePitch(Body &b, float t) {
    const double theta = PitchRate(b) * t + b.anchorPitch;
    if (theta < kNegHalfPi)
        b.pitch = kNegHalfPi;
    else if (theta < kHalfPi)
        b.pitch = static_cast<float>(theta);
    else
        b.pitch = kHalfPi;
}

// The pitch's clamp in the arcs (0x007C51B1, 0x007C5024): with theta the
// pitch at t and `inverse` the float 1/p, past +-pi/2 the time spent beyond
// it (tau), with the swept angle `delta` cut at the clamp; else 0, `delta`
// as given.
float ClampTime(const Body &b, double theta, float inverse, float &delta) {
    if (theta > kHalfPi) {
        delta = static_cast<float>(D(kHalfPi) - b.anchorPitch);
        return static_cast<float>((theta - kHalfPi) * inverse);
    }
    if (theta < kNegHalfPi) {
        delta = static_cast<float>(D(kNegHalfPi) - b.anchorPitch);
        return static_cast<float>((theta + kHalfPi) * inverse);
    }
    return 0.0f;
}

// The arcs' last step (0x007C5265, 0x007C5127): past the clamp the body
// climbs or dives straight for the time tau beyond it.
float ClampClimb(const Body &b, double z, float tau, float delta, float speed) {
    if (std::fabs(tau) < kTiny)
        return static_cast<float>(z);
    const double climb = D(tau) * speed;
    if (std::fabs((D(delta) + b.anchorPitch) - kHalfPi) < kTiny)
        return static_cast<float>(climb + z);
    return static_cast<float>(z - climb);
}

// The straight line (0x007C4ED0).
void Straight(const Body &b, float t, float (&out)[3]) {
    const double s = b.speed;
    out[0] = static_cast<float>((D(t) * b.direction[0]) * s);
    out[1] = static_cast<float>((D(t) * b.direction[1]) * s);
    out[2] = static_cast<float>(D(static_cast<float>(D(t) * b.direction[2])) * s);
}

// The turn arc (0x007C52C0).
void TurnArc(const Body &b, float t, float (&out)[3]) {
    const double s = b.speed;
    const double omega = TurnRate(b);
    const float radius = static_cast<float>(s / omega);
    const float phi = static_cast<float>(omega * t);
    const float sinPhi = static_cast<float>(std::sin(D(phi)));
    const float cosPhi = static_cast<float>(std::cos(D(phi)));
    const double a = D(sinPhi) * radius;
    const double c = D(radius) - D(cosPhi) * radius;
    const double hx = b.heading[0], hy = b.heading[1];
    out[0] = static_cast<float>((a * hx - c * hy) * b.pitchBasis[0]);
    out[1] = static_cast<float>((c * hx + a * hy) * b.pitchBasis[0]);
    out[2] = static_cast<float>((s * b.pitchBasis[1]) * t);
}

// The pitch arc (0x007C5180): on along the heading while the pitch turns,
// a circle in the vertical plane.
void PitchArc(const Body &b, float t, float (&out)[3]) {
    const float s = b.speed;
    const double p = PitchRate(b);
    const double inverse = D(kOne) / p;
    const float rv = static_cast<float>(inverse * s);
    const double pt = p * t;
    float delta = static_cast<float>(pt);
    const float tau = ClampTime(b, pt + b.anchorPitch, static_cast<float>(inverse), delta);
    const float sinD = static_cast<float>(std::sin(D(delta)));
    const float cosD = static_cast<float>(std::cos(D(delta)));
    const double h = D(sinD) * rv;
    const double v = D(rv) - D(cosD) * rv;
    out[0] = static_cast<float>((h * b.pitchBasis[0] - v * b.pitchBasis[1]) * b.heading[0]);
    out[1] = static_cast<float>((h * b.pitchBasis[0] - v * b.pitchBasis[1]) * b.heading[1]);
    const double z = v * b.pitchBasis[0] + h * b.pitchBasis[1];
    out[2] = ClampClimb(b, z, tau, delta, s);
}

// The turn and pitch arc (0x007C4FD0): both turning at once.
void TurnPitchArc(const Body &b, float t, float (&out)[3]) {
    const float s = b.speed;
    const float omega = static_cast<float>(TurnRate(b));
    const double p = PitchRate(b);
    const float rh = static_cast<float>(D(s) / omega);
    const double inverse = D(kOne) / p;
    const float rv = static_cast<float>(inverse * s);
    const double pt = p * t;
    float delta = static_cast<float>(pt);
    const float tau = ClampTime(b, pt + b.anchorPitch, static_cast<float>(inverse), delta);
    const float phi = static_cast<float>((D(t) - tau) * omega);
    const float sinP = static_cast<float>(std::sin(D(phi)));
    const float cosP = static_cast<float>(std::cos(D(phi)));
    const float sinD = static_cast<float>(std::sin(D(delta)));
    const float cosD = static_cast<float>(std::cos(D(delta)));
    const double a = (D(sinD) * rv + D(sinP) * rh) * kHalf;
    const double across = D(cosP) * rh - rh;
    const float c = static_cast<float>(D(rv) - D(cosD) * rv);
    const float x = static_cast<float>(a * b.heading[0] - across * b.heading[1]);
    const double y = across * b.heading[0] + a * b.heading[1];
    out[0] = static_cast<float>(D(x) * b.pitchBasis[0] - D(c) * b.pitchBasis[1]);
    out[1] = static_cast<float>(y);
    const double z = D(c) * b.pitchBasis[0] + D(x) * b.pitchBasis[1];
    out[2] = ClampClimb(b, z, tau, delta, s);
}

// The displacement over `ms` since the anchors (0x007C5360).
bool Displacement(Body &b, uint32_t ms, float (&out)[3]) {
    if (ms == 0)
        return false;
    const float t = static_cast<float>(D(static_cast<float>(ms)) * kMsToSec);
    int mode = 0;
    if (b.flags & 0x30) {
        IntegrateFacing(b, t);
        if (!(b.flags & FLAG_FALLING))
            mode |= 2;
    }
    if (b.flags & 0xC0) {
        IntegratePitch(b, t);
        mode |= 8;
    }
    if (b.flags & 0x3)
        mode |= 1;
    if (b.flags & 0xC)
        mode |= 4;
    switch (mode) {
    case 0:
        return true;
    case 1:
    case 4:
    case 5:
    case 12:
        Straight(b, t, out);
        return true;
    case 3:
    case 6:
    case 7:
    case 14:
        TurnArc(b, t, out);
        return true;
    case 9:
    case 13:
        PitchArc(b, t, out);
        return true;
    case 11:
    case 15:
        TurnPitchArc(b, t, out);
        return true;
    default: // 2, 8, 10: turning or pitching in place
        return false;
    }
}

// ---- the fall arc (0x007C5D20 to 0x007C6290) -----------------------------------------

double Terminal(const Body &b) { return (b.flags & 0x20000000) ? kTerminalSlow : kTerminal; }

// The fall speed's start, capped (0x007C5D40).
double CappedFallSpeed(const Body &b) {
    const double t = Terminal(b), v = b.fallSpeed;
    return v > t ? t : v;
}

// GetFallDistance (0x007C5E70).
double FallDistance(const Body &b, float t) {
    const double cap = Terminal(b), v = CappedFallSpeed(b), dt = t;
    const double end = dt * kG + v;
    if (end > cap || std::isnan(end)) {
        const double tT = (cap - v) * kInvG;
        return (v + D(kHalfG) * tT) * tT + (dt - tT) * cap;
    }
    return (dt * kHalfG + v) * dt;
}

double FallDistanceMs(const Body &b, uint32_t ms) {
    return FallDistance(b, static_cast<float>(D(static_cast<float>(ms)) * kMsToSec));
}

// The further drop from the current z to the arc at `ms` (0x007C6140,
// 0x007C6110).
double FallDrop(const Body &b, uint32_t ms) {
    const double d = FallDistanceMs(b, ms);
    if (!(b.flags & FLAG_FALLING))
        return d;
    return d + (D(b.position[2]) - b.fallStartZ);
}

// GetFallTime (0x007C5F50): the arc time at which the drop reaches `d`;
// with `first`, the earlier root.
double FallTime(const Body &b, float d, bool first) {
    const double cap = Terminal(b);
    float v = b.fallSpeed;
    if (!(D(v) <= cap) && !std::isnan(v))
        v = static_cast<float>(cap);
    if (std::fabs(v) < kParallel) {
        const double tT = D(kInvG) * cap;
        const double dT = (tT * cap) * 0.5;
        if (!(d < dT))
            return (D(d) - dT) / cap + tT;
        if (!(d > 0.0f))
            return 0.0;
        return std::sqrt(D(d) * kTwoOverG);
    }
    const double s = D(v) * v + D(d) * kTwoG;
    const double root = s > 0.0 ? std::sqrt(s) : 0.0;
    const float early = static_cast<float>((-D(v) - root) * kInvG);
    double late = (root - v) * kInvG;
    const float tT = static_cast<float>((cap - v) * kInvG);
    if (late > tT)
        late = (D(d) - (D(tT) * kHalfG + v) * tT) / cap + tT;
    if (!first)
        return late;
    return early < 0.0f ? 0.0 : D(early);
}

// GetApexTime (0x007C5DA0).
double ApexTime(const Body &b) { return (b.flags & FLAG_FALLING) ? D(b.fallSpeed) * kNegInvG : 0.0; }

// Starting a fall (0x007C61F0).
void StartFall(Body &b, float v) {
    Anchor(b, false);
    b.flags = (b.flags & 0xFBDFFFFF) | FLAG_FALLING;
    b.fallTime = 0;
    b.fallStartZ = b.position[2];
    b.fallSpeed = v;
}

// Landing (0x007C6290).
void Land(Body &b) {
    if (!(b.flags & FLAG_FALLING))
        return;
    b.flags &= 0xFFFF9FFF;
    Anchor(b, false);
    RefreshSpeed(b, false);
}

// CMovement::RestartFallAtRest (0x00633220): a head strike.
void RestartFallAtRest(Body &b) {
    b.fallSpeed = 0.0f;
    b.fallTime = 0;
    b.fallStartZ = b.position[2];
    b.flags |= FLAG_FALLING_FAR;
}

// CMovement::LatchFallingFar (0x00633240).
void LatchFallingFar(Body &b) {
    if (b.flags & 0x20004000)
        return;
    if ((b.flags & FLAG_FALLING) && !(b.fallSpeed == 0.0f)) {
        if (!(D(b.fallStartZ) - kNinth < b.position[2]))
            b.flags |= FLAG_FALLING_FAR;
        return;
    }
    if (b.fallTime >= 500)
        b.flags |= FLAG_FALLING_FAR;
}

// ---- the commands (0x007C6AE0 to 0x007C6DD0) ---------------------------------------

void StopMoveNow(Body &b) { // 0x007C6C00
    b.flags &= 0xFFFF7FFC;
    RefreshSpeed(b, false);
    Anchor(b, false);
}

void StopStrafeNow(Body &b) { // 0x007C6CE0
    b.flags &= 0xFFFEFFF3;
    RefreshSpeed(b, false);
    Anchor(b, false);
}

// The deferred direction latches (0x0061AB20), applied at a landing.
uint32_t ApplyLatches(Body &b) {
    const uint32_t entry = b.flags;
    if (b.flags & 0x8000)
        StopMoveNow(b);
    if (b.flags & 0x10000)
        StopStrafeNow(b);
    if (b.flags & 0x60000)
        StartMove(b, (b.flags & 0x20000) != 0);
    if (b.flags & 0x180000)
        StartStrafe(b, (b.flags & 0x80000) != 0);
    b.flags &= 0xFFE07FFF;
    return (b.flags ^ entry) & 0xF;
}

// ---- triangle tests (0x006333D0 to 0x006337A5) -------------------------------------

// CMovement::BoxTopTouchesTriangle (0x006333D0).
bool TopTouches(const Body &b, const Facet &f) {
    const double x = b.position[0], y = b.position[1], z = b.position[2], r = b.radius;
    const float top = static_cast<float>(z + b.height);
    const float corners[4][3] = {{static_cast<float>(x - r), static_cast<float>(y - r), top},
                                 {static_cast<float>(x + r), static_cast<float>(y - r), top},
                                 {static_cast<float>(x - r), static_cast<float>(y + r), top},
                                 {static_cast<float>(x + r), static_cast<float>(y + r), top}};
    for (const auto &c : corners)
        if (std::fabs(Dot(f.plane.n, c) + f.plane.d) < kSkin)
            return true;
    return false;
}

// CMovement::ClassifyContactTriangle (0x006334A0), for a player-driven unit
// (0x005FA550 nonzero: the floor limit is cos 50°).
bool Classify(Body &b, const Facet &f, bool &steep) {
    steep = false;
    const float nz = f.plane.n[2];
    if (nz > kCos50 || std::isnan(nz)) {
        if (Contact::PointOverFacet(f, b.position))
            b.flags &= ~FLAG_ON_STEP;
        return true;
    }
    if (nz >= 0.0f)
        return (b.flags & FLAG_ON_STEP) != 0;
    if (!TopTouches(b, f))
        return (b.flags & FLAG_ON_STEP) != 0;
    if (b.flags & FLAG_ON_STEP) {
        steep = true;
        b.flags &= ~FLAG_ON_STEP;
    }
    const double up = -D(nz);
    return up > kCos50 || std::isnan(up);
}

// IsListedTriangleSteep (0x006336A0).
bool Steep(float nz) {
    if (nz > kCos50 || std::isnan(nz))
        return false;
    if (!(nz < 0.0f))
        return true;
    return !(-D(nz) > kCos50);
}

// CMovement::LandsOnTriangle (0x00633720).
bool LandsOn(const Body &b, const Facet &f, const float (&move)[3]) {
    const float p[3] = {static_cast<float>(D(move[0]) + b.position[0]),
                        static_cast<float>(D(b.position[1]) + move[1]),
                        static_cast<float>(D(b.position[2]) + move[2])};
    const float nz = f.plane.n[2];
    if (!(nz > kCos50 || std::isnan(nz)))
        return false;
    return Contact::PointOverFacet(f, p);
}

// ---- walking helpers (0x00635C00 to 0x00636610) ------------------------------------

// SelectBevelContactNormal (0x00636610).
bool BevelDirection(const Plane *faces, uint32_t count, float (&out)[3]) {
    if (count == 0 || count > 4)
        return false;
    for (uint32_t i = 0; i < count; ++i)
        if (!(std::fabs(D(faces[i].n[2]) - kBevelZ) < kTiny))
            return false;
    switch (count) {
    case 1:
        std::memcpy(out, faces[0].n, sizeof out);
        break;
    case 2: {
        const double sx = D(faces[1].n[0]) + faces[0].n[0];
        const double sy = D(faces[1].n[1]) + faces[0].n[1];
        const float sz = static_cast<float>(D(faces[1].n[2]) + faces[0].n[2]);
        out[0] = static_cast<float>(sx * kBisect);
        out[1] = static_cast<float>(sy * kBisect);
        out[2] = static_cast<float>(D(sz) * kBisect);
        break;
    }
    case 3: {
        int alongX = -1, alongY = -1, countX = 0;
        for (int i = 0; i < 3; ++i) {
            if (std::fabs(faces[i].n[1]) < kTiny) {
                alongX = i;
                ++countX;
            } else if (alongY < 0 && std::fabs(faces[i].n[0]) < kTiny) {
                alongY = i;
            }
        }
        // Three bevels meet two along one axis: the odd one out. (The
        // client reads an unwritten slot when none is; a body's own bevels
        // always give one.)
        const int pick = countX == 1 ? alongX : (alongY >= 0 ? alongY : 0);
        std::memcpy(out, faces[pick].n, sizeof out);
        break;
    }
    default:
        out[0] = out[1] = out[2] = 0.0f;
        break;
    }
    return true;
}

// CMovement::LimitFaceRise (0x00635C00): the rise of moving `length` along
// `dir` on the plane `n`, limited to the step height k.
void Rise(const Body &b, float k, const float (&dir)[3], float &length, float (&n)[3], bool flip,
          const float (&bevel)[3], float (&out)[3]) {
    const float bevel2 = static_cast<float>((D(bevel[0]) * bevel[0] + D(bevel[1]) * bevel[1]) + D(bevel[2]) * bevel[2]);
    if (flip && !(n[2] > kCos50 || std::isnan(n[2])) && !(std::fabs(bevel2) < kParallel)) {
        n[0] = -bevel[0];
        n[1] = -bevel[1];
        n[2] = -bevel[2];
    }
    const double p = ((-D(n[2]) * dir[2] + -D(n[1]) * dir[1]) + -D(n[0]) * dir[0]) * length;
    float h;
    if (std::fabs(n[2]) < kParallel)
        h = p < 0.0 ? kNegFltMax : kFltMax;
    else
        h = static_cast<float>(p / n[2]);
    float x;
    if ((b.flags & FLAG_ON_STEP) && h < 0.0f) {
        length = 0.0f;
        x = k;
    } else if (k < h) {
        length = static_cast<float>((D(k) / h) * length);
        x = k;
    } else if (-D(k) > h) {
        length = static_cast<float>(-((D(k) / h) * length));
        x = -k;
    } else {
        x = h;
    }
    out[0] = out[1] = static_cast<float>(0.0 * x);
    out[2] = x;
}

// ComputeWallPushOut (0x00635D80): the horizontal correction that cancels
// the part of the move entering the plane, plus 0.001.
void PushOut(const float (&w)[3], float length, const float (&n)[3], float (&out)[3]) {
    if (n[2] < 0.0f && -D(n[2]) > kCos50) {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }
    const double lh = std::sqrt(D(n[0]) * n[0] + D(n[1]) * n[1]);
    double ux = n[0], uy = n[1];
    if (!(std::fabs(lh) < kParallel)) {
        const double inv = 1.0 / lh;
        ux = static_cast<float>(D(n[0]) * inv);
        uy = D(n[1]) * inv;
    }
    const double p = ((-D(n[2]) * w[2] + -D(n[1]) * w[1]) + -D(n[0]) * w[0]) * length;
    const double c = (uy * n[1] + ux * n[0]) + D(n[2]) * 0.0;
    const double q = (std::fabs(c) < kParallel ? p : p / c) + kMsToSec;
    out[0] = static_cast<float>(ux * q);
    out[1] = static_cast<float>(uy * q);
    out[2] = 0.0f;
}

// ---- the airborne resolver (0x00634960 to 0x00635600) ------------------------------

// CMovement::PlaneNearFootPoints (0x00634960).
bool PlaneNearFoot(const Body &b, const Plane &pl) {
    const double x = b.position[0], y = b.position[1], z = b.position[2], r = b.radius;
    const float waist = static_cast<float>(r * kWaist);
    const float wz = static_cast<float>(z + waist);
    const float points[5][3] = {{b.position[0], b.position[1], b.position[2]},
                                {static_cast<float>(x - r), static_cast<float>(y - r), wz},
                                {static_cast<float>(x + r), static_cast<float>(y - r), wz},
                                {static_cast<float>(x - r), static_cast<float>(y + r), wz},
                                {static_cast<float>(x + r), static_cast<float>(y + r), wz}};
    for (const auto &p : points)
        if (std::fabs(Dot(pl.n, p) + pl.d) < kSkin)
            return true;
    return false;
}

// Matrix3Determinant (0x007BE0E0), Matrix3Adjugate (0x007BE1E0),
// InvertMatrix3 (0x007BE2A0).
float Determinant(const float (&m)[9]) {
    return static_cast<float>(D(m[6]) * m[1] * m[5] + D(m[2]) * m[3] * m[7] + D(m[8]) * m[0] * m[4] -
                              D(m[6]) * m[2] * m[4] - D(m[8]) * m[1] * m[3] - D(m[0]) * m[7] * m[5]);
}

void Invert(const float (&m)[9], float det, float (&out)[9]) {
    const float adj[9] = {
        static_cast<float>(D(m[8]) * m[4] - D(m[5]) * m[7]),    static_cast<float>(-(D(m[1]) * m[8] - D(m[2]) * m[7])),
        static_cast<float>(D(m[5]) * m[1] - D(m[2]) * m[4]),    static_cast<float>(-(D(m[3]) * m[8] - D(m[5]) * m[6])),
        static_cast<float>(D(m[8]) * m[0] - D(m[2]) * m[6]),    static_cast<float>(-(D(m[5]) * m[0] - D(m[2]) * m[3])),
        static_cast<float>(D(m[7]) * m[3] - D(m[6]) * m[4]),    static_cast<float>(-(D(m[7]) * m[0] - D(m[1]) * m[6])),
        static_cast<float>(D(m[0]) * m[4] - D(m[1]) * m[3])};
    const float inverse = static_cast<float>(1.0 / det);
    for (int i = 0; i < 9; ++i)
        out[i] = static_cast<float>(D(inverse) * adj[i]);
}

// IntersectThreePlanes (0x00634FC0).
void ThreePlanes(const Plane &a, const Plane &b, const Plane &c, float (&out)[3]) {
    const float m[9] = {a.n[0], a.n[1], a.n[2], b.n[0], b.n[1], b.n[2], c.n[0], c.n[1], c.n[2]};
    const float v[3] = {-a.d, -b.d, -c.d};
    float inv[9];
    Invert(m, Determinant(m), inv);
    for (int r = 0; r < 3; ++r)
        out[r] = static_cast<float>((D(inv[r * 3 + 2]) * v[2] + D(inv[r * 3 + 1]) * v[1]) + D(inv[r * 3]) * v[0]);
}

// PointToLineDistanceSquared (0x00634F40).
double LineDistance2(const float (&p)[3], const float (&q)[3], const float (&dir)[3]) {
    const double v[3] = {D(p[0]) - q[0], D(p[1]) - q[1], D(p[2]) - q[2]};
    const double proj = (v[2] * dir[2] + v[1] * dir[1]) + v[0] * dir[0];
    const double px = proj * dir[0];
    const float py = static_cast<float>(proj * dir[1]);
    const float pz = static_cast<float>(proj * dir[2]);
    const double cx = px + q[0];
    const double cy = D(py) + q[1];
    const float cz = static_cast<float>(D(pz) + q[2]);
    const float ex = static_cast<float>(D(p[0]) - cx);
    const double ey = D(p[1]) - cy;
    const double ez = D(p[2]) - cz;
    return (ez * ez + ey * ey) + D(ex) * ex;
}

// FindNearestTriangleEdge (0x00634DA0).
void NearestEdge(const Facet &f, const float (&point)[3], const float (&crease)[3], float (&out)[3]) {
    float best = kFltMax;
    out[0] = out[1] = out[2] = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float(&v)[3] = f.v[i];
        const float(&next)[3] = f.v[(i + 1) % 3];
        const double e[3] = {D(v[0]) - next[0], D(v[1]) - next[1], D(v[2]) - next[2]};
        const double length = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
        if (std::fabs(length) < kParallel)
            continue;
        const double inv = 1.0 / length;
        const float eh[3] = {static_cast<float>(e[0] * inv), static_cast<float>(e[1] * inv),
                             static_cast<float>(e[2] * inv)};
        const double along = (D(eh[2]) * crease[2] + D(eh[1]) * crease[1]) + D(eh[0]) * crease[0];
        double metric;
        if (std::fabs(along - 1.0) < kTiny) {
            metric = LineDistance2(v, point, crease);
        } else {
            const float k[3] = {static_cast<float>(D(eh[1]) * crease[2] - D(eh[2]) * crease[1]),
                                static_cast<float>(D(eh[2]) * crease[0] - D(eh[0]) * crease[2]),
                                static_cast<float>(D(eh[0]) * crease[1] - D(eh[1]) * crease[0])};
            const double s = -((D(k[2]) * point[2] + D(k[1]) * point[1]) + D(k[0]) * point[0]);
            const double m = s + ((D(k[2]) * v[2] + D(k[1]) * v[1]) + D(k[0]) * v[0]);
            metric = m * m;
        }
        if (metric < best) {
            best = static_cast<float>(metric);
            std::memcpy(out, eh, sizeof out);
        }
    }
}

// CMovement::ComputeWallNormal (0x00634AE0).
void WallNormal(const Body &b, const Facet &f, const Plane *faces, uint32_t count, float (&out)[3]) {
    const float(&e0)[3] = faces[0].n;
    if (count == 3 || count == 4) {
        std::memcpy(out, f.plane.n, sizeof out);
        return;
    }
    if (count != 2) {
        out[0] = -e0[0];
        out[1] = -e0[1];
        out[2] = -e0[2];
        return;
    }
    const float(&e1)[3] = faces[1].n;
    const float c0[3] = {static_cast<float>(D(e0[1]) * e1[2] - D(e0[2]) * e1[1]),
                         static_cast<float>(D(e0[2]) * e1[0] - D(e0[0]) * e1[2]),
                         static_cast<float>(D(e0[0]) * e1[1] - D(e0[1]) * e1[0])};
    const double inv = 1.0 / std::sqrt((D(c0[0]) * c0[0] + D(c0[1]) * c0[1]) + D(c0[2]) * c0[2]);
    const double cz = D(c0[2]) * inv;
    const float c[3] = {static_cast<float>(D(c0[0]) * inv), static_cast<float>(D(c0[1]) * inv),
                        static_cast<float>(cz)};
    const float(&n)[3] = f.plane.n;
    if (std::fabs(cz) < kTiny || std::fabs((D(c[2]) * n[2] + D(c[1]) * n[1]) + D(c[0]) * n[0]) < kTiny) {
        std::memcpy(out, n, sizeof out);
        return;
    }
    if (!(std::fabs(D(c[2]) - 1.0) < kTiny) && PlaneNearFoot(b, f.plane)) {
        std::memcpy(out, n, sizeof out);
        return;
    }
    float corner[3], edge[3];
    ThreePlanes(f.plane, faces[0], faces[1], corner);
    NearestEdge(f, corner, c, edge);
    const float m0[3] = {static_cast<float>(D(edge[1]) * c[2] - D(edge[2]) * c[1]),
                         static_cast<float>(D(edge[2]) * c[0] - D(edge[0]) * c[2]),
                         static_cast<float>(D(edge[0]) * c[1] - D(edge[1]) * c[0])};
    const double ml = std::sqrt((D(m0[0]) * m0[0] + D(m0[1]) * m0[1]) + D(m0[2]) * m0[2]);
    if (ml < kParallel) {
        out[0] = -e0[0];
        out[1] = -e0[1];
        out[2] = -e0[2];
        return;
    }
    const double mi = 1.0 / ml;
    const float m[3] = {static_cast<float>(D(m0[0]) * mi), static_cast<float>(D(m0[1]) * mi),
                        static_cast<float>(D(m0[2]) * mi)};
    const double d = (D(m[2]) * e0[2] + D(m[1]) * e0[1]) + D(m[0]) * e0[0];
    const float k = d > 0.0 ? -1.0f : 1.0f;
    out[0] = m[0] * k;
    out[1] = m[1] * k;
    out[2] = m[2] * k;
}

// CMovement::ComputeWallPush (0x00635090): the horizontal push that undoes
// the rest of the move's part into the wall.
void WallPush(const Body &b, const float (&u)[3], float a, float length, const Facet &f, const Plane *faces,
              uint32_t count, float (&out)[2]) {
    float w[3];
    if (Steep(f.plane.n[2])) {
        WallNormal(b, f, faces, count, w);
    } else {
        w[0] = -faces[0].n[0];
        w[1] = -faces[0].n[1];
        w[2] = -faces[0].n[2];
    }
    const double lh = std::sqrt(D(w[0]) * w[0] + D(w[1]) * w[1]);
    double hx = w[0], hy = w[1];
    if (!(std::fabs(lh) < kParallel)) {
        const double inv = 1.0 / lh;
        hx = static_cast<float>(D(w[0]) * inv);
        hy = D(w[1]) * inv;
    }
    const double p = ((-D(w[2]) * u[2] + -D(w[1]) * u[1]) + -D(w[0]) * u[0]) * (D(length) - a);
    const double den = (D(w[1]) * hy + hx * w[0]) + D(w[2]) * 0.0;
    const double q = std::fabs(den) < kParallel ? p : p / den;
    out[0] = static_cast<float>(hx * q);
    out[1] = static_cast<float>(hy * q);
}

bool AnyFaceZ(const Plane *faces, uint32_t count, float z) {
    for (uint32_t i = 0; i < count && i < 9; ++i)
        if (std::fabs(D(faces[i].n[2]) - z) < kTiny)
            return true;
    return false;
}

// One airborne step (0x006351A0): 0 clear, 1 hit, 2 the sweep failed.
int AirStep(Body &b, float t, float (&move)[3], float &a, float (&push)[2], bool &land, bool &head) {
    land = head = false;
    const float length = a;
    if (std::fabs(length) < kParallel)
        return 0;
    const double inv = 1.0 / length;
    const float u[3] = {static_cast<float>(inv * move[0]), static_cast<float>(inv * move[1]),
                        static_cast<float>(inv * move[2])};
    const Hit hit = Sweep(b, u, length);
    a = hit.distance;
    if (!hit.ok) {
        move[0] = move[1] = move[2] = 0.0f;
        return 2;
    }
    if (hit.index >= FacetCount(b))
        return 0;
    move[0] = static_cast<float>(D(u[0]) * a);
    move[1] = static_cast<float>(D(u[1]) * a);
    move[2] = static_cast<float>(D(a) * u[2]);
    const Facet &f = b.facets.solid[hit.index];
    if (LandsOn(b, f, move)) {
        land = true;
        return 1;
    }
    if (u[2] > 0.0f) {
        const double r = ApexTime(b) - t;
        if (r >= 0.0 && r * b.speed >= a && AnyFaceZ(hit.faces, hit.count, 1.0f)) {
            head = true;
            return 1;
        }
    }
    WallPush(b, u, a, length, f, hit.faces, hit.count, push);
    return 1;
}

// CMovement::IsRisingThroughStep (0x00635550).
bool RisingThrough(const Body &b, float t, double remaining, const float (&move)[3], bool head) {
    if (head)
        return true;
    if (b.fallSpeed >= 0.0f)
        return false;
    const float apex = static_cast<float>(ApexTime(b));
    if (D(t) + remaining < apex)
        return true;
    if (t > apex)
        return false;
    const double m = (D(apex) - t) * b.speed;
    return m * m > D(move[1]) * move[1] + D(move[0]) * move[0];
}

// CMovement::LimitStepByFall (0x00635450): the step's time from the arc.
double StepTime(Body &b, double remaining, float t, float (&move)[3], float &a, bool head, float horizontal) {
    const bool rising = RisingThrough(b, t, remaining, move, head);
    float k = 0.0f;
    if (horizontal > kTiny)
        k = static_cast<float>((std::sqrt(D(move[1]) * move[1] + D(move[0]) * move[0]) / horizontal) * remaining);
    const float drop = static_cast<float>((D(b.fallStartZ) - b.position[2]) - move[2]);
    const double arc = FallTime(b, drop, rising);
    if (arc <= t) {
        move[0] = move[1] = move[2] = 0.0f;
        a = 0.0f;
        return 0.0;
    }
    const double d = arc - t;
    if (d > remaining)
        return remaining;
    if (d < k) {
        const double s = d / k;
        const double sx = s * move[0], sy = s * move[1];
        move[0] = static_cast<float>(sx);
        move[1] = static_cast<float>(sy);
        a = static_cast<float>(std::sqrt((sy * sy + sx * sx) + D(move[2]) * move[2]));
    }
    return d;
}

// CMovement::ResolveAirborneMove (0x00635600).
uint32_t Airborne(Body &b, uint32_t ms, const float (&moveIn)[3], bool report) {
    const float total = static_cast<float>(D(static_cast<float>(ms)) * kMsToSec);
    const float fallStart = static_cast<float>(D(static_cast<float>(b.fallTime)) * kMsToSec);
    float used = 0.0f;
    const float savedSpeed = b.speed;
    const float savedHeading[2] = {b.heading[0], b.heading[1]};
    float savedDirection[3];
    std::memcpy(savedDirection, b.direction, sizeof savedDirection);
    const float start[2] = {b.position[0], b.position[1]};

    float move[3] = {moveIn[0], moveIn[1], moveIn[2]};
    float length = static_cast<float>(std::sqrt((D(move[0]) * move[0] + D(move[1]) * move[1]) + D(move[2]) * move[2]));
    const float h0 = static_cast<float>(std::sqrt(D(move[1]) * move[1] + D(move[0]) * move[0]));
    const float z0 = static_cast<float>(std::fabs(D(move[2])));
    float horizontal = h0;
    float push[2] = {0.0f, 0.0f};
    uint32_t stalls = 0;
    bool forced = false, continued = false, failed = false, head = false;
    double finish = total;

    for (;;) {
        if (std::fabs(length) < kTiny)
            break;
        const double inv = D(kOne) / length;
        const float u[3] = {static_cast<float>(inv * move[0]), static_cast<float>(inv * move[1]),
                            static_cast<float>(inv * move[2])};
        float a = length;
        const float t = static_cast<float>(D(fallStart) + used);
        bool land;
        const int step = AirStep(b, t, move, a, push, land, head);
        const double remaining = D(total) - used;
        if (step == 2) {
            failed = true;
            break;
        }
        const double tau = step == 1 ? StepTime(b, remaining, t, move, a, head, horizontal) : remaining;
        for (int k = 0; k < 3; ++k)
            b.position[k] = static_cast<float>(D(move[k]) + b.position[k]);
        used = static_cast<float>(D(used) + tau);
        if (head) {
            RestartFallAtRest(b);
            finish = used;
            break;
        }
        if (land) {
            Land(b);
            finish = used;
            break;
        }
        if (D(used) + kStall >= total)
            break;
        length = static_cast<float>(D(length) - a);
        continued = true;
        if (!(tau > kStall)) {
            if (++stalls > 5) {
                if (forced || std::fabs(horizontal) < kTiny) {
                    Land(b);
                    finish = used;
                    break;
                }
                // A forced drop: straight down for the rest.
                b.speed = 0.0f;
                const double dz = D(u[2]) * length;
                move[0] = move[1] = 0.0f;
                move[2] = static_cast<float>(dz);
                stalls = 0;
                forced = true;
                length = static_cast<float>(std::fabs(dz));
                continue;
            }
        } else {
            stalls = 1;
        }
        move[0] = static_cast<float>(D(u[0]) * length + push[0]);
        move[1] = static_cast<float>(D(u[1]) * length + push[1]);
        move[2] = static_cast<float>(D(u[2]) * length);
        if (stalls >= 2) {
            const float ax = static_cast<float>((D(b.position[0]) - start[0]) + move[0]);
            const double ay = (D(b.position[1]) - start[1]) + move[1];
            const double reach = std::sqrt(ay * ay + D(ax) * ax);
            if (reach > D(h0) + kTiny && reach > D(z0) * kStallReach) {
                Land(b);
                finish = used;
                break;
            }
        }
        // The heading after contact.
        const double flat2 = D(move[1]) * move[1] + D(move[0]) * move[0];
        horizontal = static_cast<float>(std::sqrt(flat2));
        float nx, ny;
        if (std::fabs(horizontal) < kTiny) {
            nx = move[0];
            ny = move[1];
            b.speed = 0.0f;
        } else {
            const double invH = D(kOne) / horizontal;
            nx = static_cast<float>(D(move[0]) * invH);
            const double ny53 = invH * move[1];
            ny = static_cast<float>(ny53);
            const double v = (ny53 * b.heading[1] + D(nx) * b.heading[0]) * b.speed;
            b.speed = static_cast<float>(v);
            if (v < 0.0)
                b.speed = 0.0f;
        }
        b.heading[0] = nx;
        b.heading[1] = ny;
        b.direction[0] = nx;
        b.direction[1] = ny;
        b.direction[2] = 0.0f;
        length = static_cast<float>(std::sqrt(flat2 + D(move[2]) * move[2]));
    }

    if (failed) {
        const int32_t lost = Round((D(total) - used) * kThousand);
        b.elapsed -= static_cast<uint32_t>(lost);
        b.fallTime -= static_cast<uint32_t>(lost);
        finish = total;
    }
    const int32_t n = Round(finish * kThousand);
    if (!head)
        b.fallTime += static_cast<uint32_t>(n);
    if (report) {
        if (!(b.flags & FLAG_FALLING)) {
            if (ApplyLatches(b) != 0)
                return static_cast<uint32_t>(n);
        } else {
            LatchFallingFar(b);
            if ((b.flags & FLAG_FALLING) && !(b.fallSpeed == 0.0f) && (b.flags & 0xF)) {
                b.speed = savedSpeed;
                b.heading[0] = savedHeading[0];
                b.heading[1] = savedHeading[1];
                std::memcpy(b.direction, savedDirection, sizeof b.direction);
            }
        }
    }
    if (continued)
        Anchor(b, false);
    return static_cast<uint32_t>(n);
}

// ---- the walk resolver (0x00635F80 to 0x006367B0) ----------------------------------

// The 56 bytes 0x00635EA0 saves and 0x00635F10 restores.
struct Motion {
    float anchor[3], anchorFacing, anchorPitch;
    uint32_t elapsed, fallTime;
    float direction[3], heading[2];
    uint32_t flags;
    float speed;
};

Motion SaveMotion(const Body &b) {
    Motion m;
    std::memcpy(m.anchor, b.anchor, sizeof m.anchor);
    m.anchorFacing = b.anchorFacing;
    m.anchorPitch = b.anchorPitch;
    m.elapsed = b.elapsed;
    m.fallTime = b.fallTime;
    std::memcpy(m.direction, b.direction, sizeof m.direction);
    std::memcpy(m.heading, b.heading, sizeof m.heading);
    m.flags = b.flags;
    m.speed = b.speed;
    return m;
}

void RestoreMotion(Body &b, const Motion &m) {
    std::memcpy(b.anchor, m.anchor, sizeof b.anchor);
    b.anchorFacing = m.anchorFacing;
    b.anchorPitch = m.anchorPitch;
    b.elapsed = m.elapsed;
    b.fallTime = m.fallTime;
    std::memcpy(b.direction, m.direction, sizeof b.direction);
    std::memcpy(b.heading, m.heading, sizeof b.heading);
    b.flags = m.flags;
    b.speed = m.speed;
}

// CMovement::ProbeFallAway (0x00635F80): whether a free fall of `drop`
// carries the body a radius from `start` within 10° of `dir`. The position,
// +0x7C and +0xA0 stay as the simulated fall left them.
bool FallAway(Body &b, const float (&dir)[2], const float (&start)[2], float drop) {
    const double h = D(drop) + kTenth;
    const uint32_t total = static_cast<uint32_t>(Round(std::sqrt((h + h) * kInvG) * kThousand));
    const Motion saved = SaveMotion(b);
    StartFall(b, 0.0f);
    const float move[3] = {b.heading[0], b.heading[1], static_cast<float>(-FallDrop(b, total))};
    for (uint32_t done = 0; done < total && (b.flags & FLAG_FALLING);)
        done += Airborne(b, total - done, move, false);
    RestoreMotion(b, saved);
    const float dx = static_cast<float>(D(b.position[0]) - start[0]);
    const double dy53 = D(b.position[1]) - start[1];
    const float dy = static_cast<float>(dy53);
    const double distance = std::sqrt(dy53 * dy + D(dx) * dx);
    if (distance < b.radius)
        return false;
    const double inv = D(kOne) / distance;
    const double ux = D(dx) * inv, uy = D(dy) * inv;
    return D(dir[1]) * uy + ux * dir[0] > kCos10;
}

// The step height (0x00617430): +0xB8 for a player-driven unit.
float StepHeight(const Body &b) { return b.stepRatio; }

// CMovement::ProbeStepUp (0x00636100): 0 a sweep failed, 1 blocked, 2 the
// step is allowed.
int StepUp(Body &b, const float (&along)[2], const float (&n)[3]) {
    const float saved[3] = {b.position[0], b.position[1], b.position[2]};
    const float k = StepHeight(b);
    const float rPlus = static_cast<float>(D(b.radius) + kSkin);
    const float reach = D(k) * kTan50 > rPlus ? static_cast<float>(D(k) * kTan50) : rPlus;
    float sx = along[0], sy = along[1];
    if (!(n[2] > kCos50 || std::isnan(n[2]) || n[2] < 0.0f)) {
        const double inv = 1.0 / std::sqrt(D(n[1]) * n[1] + D(n[0]) * n[0]);
        sx = -static_cast<float>(inv * n[0]);
        sy = -static_cast<float>(D(n[1]) * inv);
        const float back[3] = {sx, sy, 0.0f};
        const Hit probe = Sweep(b, back, reach);
        if (!probe.ok)
            return 0;
        const bool same = probe.index < FacetCount(b) && b.facets.solid[probe.index].plane.n[0] == n[0] &&
                          b.facets.solid[probe.index].plane.n[1] == n[1] &&
                          b.facets.solid[probe.index].plane.n[2] == n[2];
        if (!same) {
            sx = along[0];
            sy = along[1];
        }
    }
    const float upDir[3] = {0.0f, 0.0f, 1.0f};
    const Hit upHit = Sweep(b, upDir, k);
    if (!upHit.ok)
        return 0;
    float up = upHit.distance;
    if (std::fabs(up) < kParallel)
        return 1;
    b.position[2] = static_cast<float>(D(b.position[2]) + up);
    const float across[3] = {sx, sy, 0.0f};
    const Hit over = Sweep(b, across, reach);
    if (!over.ok)
        return 0;
    const float f = over.distance;
    b.position[0] = static_cast<float>(D(b.position[0]) + D(sx) * f);
    b.position[1] = static_cast<float>(D(b.position[1]) + D(sy) * f);
    uint32_t last = over.index;
    float travelled = f;
    if (!(std::fabs(D(f) - reach) < kTiny) && over.index < FacetCount(b)) {
        const Facet &face = b.facets.solid[over.index];
        bool steep;
        if (Classify(b, face, steep)) {
            float length = static_cast<float>(D(reach) - f);
            float bevel[3] = {0.0f, 0.0f, 0.0f};
            BevelDirection(over.faces, over.count, bevel);
            float n2[3] = {face.plane.n[0], face.plane.n[1], face.plane.n[2]};
            float rise[3];
            Rise(b, k, across, length, n2, true, bevel, rise);
            const float nv[3] = {static_cast<float>(D(rise[0]) + D(sx) * length),
                                 static_cast<float>(D(rise[1]) + D(sy) * length), rise[2]};
            const double l = std::sqrt((D(nv[0]) * nv[0] + D(nv[1]) * nv[1]) + D(nv[2]) * nv[2]);
            const float lf = static_cast<float>(l);
            if (!(std::fabs(l) < kParallel)) {
                const double inv = 1.0 / lf;
                const float nd[3] = {static_cast<float>(D(nv[0]) * inv), static_cast<float>(D(nv[1]) * inv),
                                     static_cast<float>(inv * nv[2])};
                const Hit slide = Sweep(b, nd, lf);
                if (!slide.ok)
                    return 0;
                const double dx = D(nd[0]) * slide.distance, dy = D(nd[1]) * slide.distance;
                const float dz = static_cast<float>(D(nd[2]) * slide.distance);
                travelled = static_cast<float>(D(f) + std::sqrt(dx * dx + dy * dy));
                b.position[0] = static_cast<float>(D(b.position[0]) + dx);
                b.position[1] = static_cast<float>(D(b.position[1]) + dy);
                b.position[2] = static_cast<float>(D(b.position[2]) + dz);
                up = static_cast<float>(D(up) + dz);
                last = slide.index;
            }
        }
    }
    const auto allowed = [&] {
        std::memcpy(b.position, saved, sizeof b.position);
        if (!(b.flags & FLAG_ON_STEP))
            b.stepFloor = saved[2];
        return 2;
    };
    if (last != FacetCount(b) && !(travelled > b.radius)) {
        std::memcpy(b.position, saved, sizeof b.position);
        return 1;
    }
    const float downDir[3] = {0.0f, 0.0f, -1.0f};
    const Hit down = Sweep(b, downDir, static_cast<float>(D(up) + kDownBias));
    if (!down.ok)
        return 0;
    b.position[2] = static_cast<float>(D(b.position[2]) - down.distance);
    if (down.index >= FacetCount(b))
        return allowed();
    const float floorZ = b.facets.solid[down.index].plane.n[2];
    if (floorZ > kCos50 || std::isnan(floorZ))
        return allowed();
    const float drop = static_cast<float>(D(up) - down.distance);
    const float from[2] = {saved[0], saved[1]};
    const float way[2] = {sx, sy};
    if (FallAway(b, way, from, drop))
        return allowed();
    std::memcpy(b.position, saved, sizeof b.position);
    return 1;
}

// CMovement::ResolveWalk (0x006367B0): a grounded body moved `dist` along
// (dir, 0) within `ms`. The hover branches are left out.
uint32_t Walk(Body &b, uint32_t ms, float dist, const float (&dir)[2]) {
    if (std::fabs(dist) < kTiny)
        return ms;
    if (FacetCount(b) == 0) {
        StartFall(b, 0.0f);
        return 0;
    }
    const float total = static_cast<float>(ms);
    const float k = StepHeight(b);
    float slide[3] = {dir[0], dir[1], 0.0f};
    float hx = dir[0], hy = dir[1];
    float length = dist, budget = dist, used = 0.0f, segment = total;
    bool reanchor = false, noFloor = false;
    uint32_t shortHits = 0;

    Hit hit = Sweep(b, slide, dist);
    if (!hit.ok)
        return ms;
    bool settle = false;
    for (;;) {
        float delta[3];
        for (int i = 0; i < 3; ++i)
            delta[i] = static_cast<float>(D(slide[i]) * hit.distance);
        for (int i = 0; i < 3; ++i)
            b.position[i] = static_cast<float>(D(delta[i]) + b.position[i]);
        if (hit.index >= FacetCount(b)) {
            used = static_cast<float>(D(segment) + used);
            const double rest = D(total) - used;
            if (rest < 1.0 || std::isnan(rest)) {
                settle = true;
                break;
            }
            segment = static_cast<float>(rest);
            slide[0] = dir[0];
            slide[1] = dir[1];
            slide[2] = 0.0f;
            hx = dir[0];
            hy = dir[1];
            length = static_cast<float>(rest / total * dist);
        } else {
            const double dt = D(hit.distance) / length * segment;
            length = static_cast<float>(D(length) - hit.distance);
            budget = static_cast<float>(D(budget) - std::sqrt(D(delta[0]) * delta[0] + D(delta[1]) * delta[1]));
            if (dt < 1.0 || std::isnan(dt)) {
                if (++shortHits > 5) {
                    reanchor = true;
                    break;
                }
            } else {
                shortHits = 1;
            }
            used = static_cast<float>(D(used) + dt);
            segment = static_cast<float>(D(segment) - dt);
            if (D(total) - used < 1.0) {
                if (!(std::fabs(D(total) - used) < kTiny))
                    reanchor = true;
                break;
            }
            const Facet &face = b.facets.solid[hit.index];
            bool steep;
            const bool walkable = Classify(b, face, steep);
            noFloor = steep;
            float response[3];
            float bevel[3] = {0.0f, 0.0f, 0.0f};
            float n[3] = {face.plane.n[0], face.plane.n[1], face.plane.n[2]};
            if (walkable) {
                const float before = length;
                const bool flip = BevelDirection(hit.faces, hit.count, bevel);
                Rise(b, k, slide, length, n, flip, bevel, response);
                segment = static_cast<float>(D(length) / before * segment);
            } else {
                if (steep)
                    break;
                reanchor = true;
                const bool flip = BevelDirection(hit.faces, hit.count, bevel);
                const float along[2] = {hx, hy};
                const int code = StepUp(b, along, n);
                if (code == 0)
                    return ms;
                if (code == 2) {
                    b.flags |= FLAG_ON_STEP;
                    const float before = length;
                    Rise(b, k, slide, length, n, flip, bevel, response);
                    segment = static_cast<float>(D(length) / before * segment);
                } else {
                    PushOut(slide, length, n, response);
                }
            }
            // The deflected move.
            const float sz = static_cast<float>(D(slide[2]) * length);
            const float mx = static_cast<float>(D(slide[0]) * length + response[0]);
            const float my = static_cast<float>(D(response[1]) + D(slide[1]) * length);
            const float mz = static_cast<float>(D(response[2]) + sz);
            const float mx2 = static_cast<float>(D(mx) * mx);
            const float my2 = static_cast<float>(D(my) * my);
            const double newLength = std::sqrt((D(mz) * mz + my2) + mx2);
            length = static_cast<float>(newLength);
            if (newLength < kMsToSec) {
                reanchor = true;
                break;
            }
            hx = mx;
            hy = my;
            const double inv = D(kOne) / length;
            slide[0] = static_cast<float>(D(mx) * inv);
            slide[1] = static_cast<float>(D(my) * inv);
            slide[2] = static_cast<float>(inv * mz);
            const double oldBudget = budget;
            const double flat = std::sqrt(D(my2) + mx2);
            budget = static_cast<float>(flat);
            if (!(std::fabs(flat) < kParallel)) {
                const double invH = D(kOne) / budget;
                hx = static_cast<float>(D(mx) * invH);
                hy = static_cast<float>(D(my) * invH);
            }
            if (oldBudget - flat > kTiny)
                reanchor = true;
            // A rise past the step's top is cut to it.
            if ((b.flags & FLAG_ON_STEP) && mz > kTiny) {
                const float target = static_cast<float>(D(mz) + b.position[2]);
                if (D(k) + b.stepFloor < target) {
                    const double room = D(k) + b.stepFloor - b.position[2];
                    length = static_cast<float>(room / mz * length);
                    if (!(std::fabs(room - mz) < kTiny))
                        reanchor = true;
                    if (length < kMsToSec) {
                        noFloor = true;
                        break;
                    }
                }
            }
        }
        hit = Sweep(b, slide, length);
        if (!hit.ok)
            return ms;
    }

    if (settle) {
        // Down onto the ground.
        float reachDown = static_cast<float>(D(hit.distance) * kWaist + kThirtySixth);
        if (b.flags & FLAG_ON_STEP)
            reachDown = static_cast<float>(D(k) + reachDown);
        const float downDir[3] = {0.0f, 0.0f, -1.0f};
        hit = Sweep(b, downDir, reachDown);
        if (!hit.ok)
            return ms;
        b.position[2] = static_cast<float>(D(b.position[2]) - hit.distance);
        if (hit.index >= FacetCount(b)) {
            noFloor = true;
        } else {
            float n[3];
            std::memcpy(n, b.facets.solid[hit.index].plane.n, sizeof n);
            if (n[2] > kCos50 || std::isnan(n[2])) {
                b.flags &= ~FLAG_ON_STEP;
            } else {
                const float along[2] = {hx, hy};
                const int code = StepUp(b, along, n);
                if (code == 0)
                    return ms;
                if (code == 1)
                    noFloor = true;
                else
                    b.flags |= FLAG_ON_STEP;
            }
        }
    }
    if (hit.index >= FacetCount(b) || noFloor) {
        StartFall(b, 0.0f);
        return ms;
    }
    if (reanchor)
        Anchor(b, false);
    return ms;
}

// CMovement fall step (0x00635B00).
uint32_t FallStep(Body &b, uint32_t ms, float dist, const float (&dir)[2]) {
    if (ms == 0)
        return 0;
    const double negDrop = -FallDrop(b, b.fallTime + ms);
    const float dz = static_cast<float>(negDrop);
    const float dx = static_cast<float>(D(dist) * dir[0]);
    const float dy = static_cast<float>(D(dist) * dir[1]);
    const double magnitude = std::sqrt((negDrop * dz + D(dy) * dy) + D(dx) * dx);
    if (std::fabs(magnitude) < kParallel) {
        b.fallTime += ms;
        return ms;
    }
    if (FacetCount(b) == 0) {
        b.position[0] = static_cast<float>(D(dx) + b.position[0]);
        b.position[1] = static_cast<float>(D(dy) + b.position[1]);
        b.position[2] = static_cast<float>(D(dz) + b.position[2]);
        b.fallTime += ms;
        LatchFallingFar(b);
        return ms;
    }
    const float move[3] = {dx, dy, dz};
    return Airborne(b, ms, move, true);
}

// ---- the substep loop (0x00633840, 0x00634040) -------------------------------------

void Union(float (&box)[6], const float (&other)[6]) {
    for (int i = 0; i < 3; ++i) {
        if (box[i] > other[i])
            box[i] = other[i];
        if (other[i + 3] > box[i + 3])
            box[i + 3] = other[i + 3];
    }
}

// CMovement::QuerySweptBox (0x00633840): the facets in the body's box grown
// along the substep's move, and a swimmer's liquid in the same box.
bool QuerySwept(Body &b, float dist, uint32_t ms, const float (&u)[3]) {
    const double r = b.radius;
    float box[6];
    Contact::BaseBox(b.position, b.radius, b.height, box);
    float m[3] = {static_cast<float>(D(u[0]) * dist), static_cast<float>(D(u[1]) * dist),
                  static_cast<float>(D(u[2]) * dist)};
    if (b.flags & FLAG_FALLING) {
        m[2] = static_cast<float>(D(m[2]) - FallDrop(b, b.fallTime + ms));
        float moved[6];
        for (int i = 0; i < 6; ++i)
            moved[i] = static_cast<float>(D(box[i]) + m[i % 3]);
        Union(box, moved);
        const double t = D(m[2]) * kNegTan50;
        if (t > 0.0) {
            for (int i = 0; i < 2; ++i) {
                box[i] = static_cast<float>(D(box[i]) - t);
                box[i + 3] = static_cast<float>(D(box[i + 3]) + t);
            }
        }
    } else if (b.flags & FLAG_SWIMMING) {
        // The box about the move's midpoint, half the move each way, widened
        // to the body's diagonal and raised by its height (0x00633B5E).
        const float half = static_cast<float>(D(dist) * kHalf);
        float c[3];
        for (int i = 0; i < 3; ++i)
            c[i] = static_cast<float>(D(static_cast<float>(D(u[i]) * half)) + b.position[i]);
        const float q = static_cast<float>(r * kSqrt2);
        float around[6];
        for (int i = 0; i < 3; ++i) {
            around[i] = static_cast<float>(D(c[i]) - half);
            around[i + 3] = static_cast<float>(D(c[i]) + half);
        }
        around[0] = static_cast<float>(D(around[0]) - q);
        around[3] = static_cast<float>(D(around[3]) + q);
        around[1] = static_cast<float>(D(around[1]) - q);
        around[4] = static_cast<float>(D(around[4]) + q);
        const float h = b.height;
        around[5] = static_cast<float>(D(h < q ? q : h) + around[5]);
        Union(box, around);
    } else {
        const double k = StepHeight(b);
        const float rPlus = static_cast<float>(r + kSkin);
        const double grow = k * kTan50 > rPlus ? k * kTan50 : D(rPlus);
        float moved[6];
        for (int i = 0; i < 6; ++i)
            moved[i] = static_cast<float>(D(box[i]) + static_cast<float>(D(u[i % 3]) * (grow + dist)));
        Union(box, moved);
        const float half = static_cast<float>(D(dist) * kHalf);
        float centre[3];
        for (int i = 0; i < 3; ++i)
            centre[i] = static_cast<float>(D(static_cast<float>(D(u[i]) * half)) + b.position[i]);
        const double lateral = r * kSqrt2 + half;
        const float side[6] = {static_cast<float>(D(centre[0]) - lateral), static_cast<float>(D(centre[1]) - lateral),
                               centre[2], static_cast<float>(D(centre[0]) + lateral),
                               static_cast<float>(D(centre[1]) + lateral), centre[2]};
        Union(box, side);
        const double twoK = k + k;
        box[5] = static_cast<float>((twoK > dist ? twoK : D(dist)) + box[5]);
        box[2] = static_cast<float>(D(box[2]) - (k + D(dist) * kTan50));
    }
    for (int i = 0; i < 3; ++i) {
        box[i] = static_cast<float>(D(box[i]) - kSkin);
        box[i + 3] = static_cast<float>(D(box[i + 3]) + kSkin);
    }
    if (!b.facets.query(b.facets.context, box, b.facets.solid))
        return false;
    if (b.flags & FLAG_SWIMMING)
        Contact::QueryLiquid(b.facets, box);
    std::memcpy(b.facets.box, box, sizeof box);
    return true;
}

// CMovement's slide off a facet (0x00634300): the part of `rem` along `u`
// that goes into facet `hit` of `list`, pushed back out along its normal.
void SlideOff(const std::vector<Facet> &list, uint32_t hit, const float (&u)[3], float rem, float (&out)[3]) {
    const Plane &n = list[hit].plane;
    const double k = ((-D(n.n[2]) * u[2] + -D(n.n[1]) * u[1]) + -D(n.n[0]) * u[0]) * rem;
    const float kf = static_cast<float>(k);
    out[0] = static_cast<float>(D(kf) * n.n[0]);
    out[1] = static_cast<float>(D(kf) * n.n[1]);
    out[2] = static_cast<float>(k * n.n[2]);
}

// Moves the body `a` along `u` (0x00634774, 0x006347AC): x and y at 53 bits,
// z through a float.
void MoveAlong(Body &b, const float (&u)[3], float a) {
    const float dz = static_cast<float>(D(u[2]) * a);
    b.position[0] = static_cast<float>(D(u[0]) * a + b.position[0]);
    b.position[1] = static_cast<float>(D(u[1]) * a + b.position[1]);
    b.position[2] = static_cast<float>(D(dz) + b.position[2]);
}

// CMovement::ResolveFreeMove (0x00634640): a swimmer's move, `dist` along
// `u` over `ms`, with no gravity and no steps: it sweeps the solid list, and
// while no solid facet has stopped it in this call the liquid list for what
// is left, and slides along what it meets until the time is used, the move
// is spent, or five passes in a row make no progress. Every end but a
// failed sweep re-anchors, except a move that ended before any slide
// without a hit or with no time left. It reports all of `ms` used.
uint32_t FreeMove(Body &b, uint32_t ms, float dist, float (&u)[3]) {
    if (ms == 0)
        return 0;
    if (std::fabs(dist) < kTiny)
        return ms;
    const float total = static_cast<float>(ms);
    float used = 0.0f;
    float rem = dist;
    uint32_t stalls = 0;
    bool solidHit = false, slid = false;
    Hit h = Sweep(b, u, dist);
    if (!h.ok)
        return ms;
    for (;;) {
        bool liquid = false;
        if (h.index < FacetCount(b)) {
            solidHit = true;
        } else {
            if (!solidHit && !(b.flags & 0x800)) {
                h = Sweep(b, u, rem, true);
                if (!h.ok)
                    return ms;
                liquid = true;
            }
            const std::vector<Facet> &list = liquid ? b.facets.liquid : b.facets.solid;
            if (h.index == list.size()) {
                MoveAlong(b, u, h.distance);
                if (slid)
                    Anchor(b, false);
                return ms;
            }
        }
        MoveAlong(b, u, h.distance);
        const double t = (D(total) - used) * (D(h.distance) / rem);
        rem = static_cast<float>(D(rem) - h.distance);
        if (D(kTiny) + t > kOne) {
            used = static_cast<float>(t + used);
            stalls = 1;
        } else if (++stalls > 5) {
            break;
        }
        if (D(total) - used < kOne) {
            if (!(std::fabs(D(total) - used) < kTiny) || slid)
                break;
            return ms;
        }
        float push[3];
        SlideOff(liquid ? b.facets.liquid : b.facets.solid, h.index, u, rem, push);
        slid = true;
        const float dz = static_cast<float>(D(u[2]) * rem);
        const float m[3] = {static_cast<float>(D(u[0]) * rem + push[0]),
                            static_cast<float>(D(push[1]) + D(u[1]) * rem), static_cast<float>(D(push[2]) + dz)};
        rem = static_cast<float>(std::sqrt((D(m[2]) * m[2] + D(m[1]) * m[1]) + D(m[0]) * m[0]));
        if (std::fabs(rem) < kTiny)
            break;
        const double inverse = D(kOne) / rem;
        for (int i = 0; i < 3; ++i)
            u[i] = static_cast<float>(D(m[i]) * inverse);
        h = Sweep(b, u, rem);
        if (!h.ok)
            return ms;
    }
    Anchor(b, false);
    return ms;
}

// CMovement::ResolveDisplacement (0x00634040): `d` over `ms` in substeps.
uint32_t Resolve(Body &b, uint32_t ms, const float (&d)[3]) {
    if (ms == 0)
        return 0;
    const double length = std::sqrt((D(d[0]) * d[0] + D(d[1]) * d[1]) + D(d[2]) * d[2]);
    const float speed = static_cast<float>(length / (D(static_cast<float>(ms)) * kMsToSec));
    float u[3] = {0.0f, 0.0f, 0.0f};
    if (!(std::fabs(length) < kTiny)) {
        const double inv = D(kOne) / length;
        for (int i = 0; i < 3; ++i)
            u[i] = static_cast<float>(inv * d[i]);
    }
    uint32_t done = 0;
    for (int pass = 0; pass < kMaxPasses && (b.flags & 0x200F); ++pass) {
        const uint32_t rest = ms - done;
        const float dist = static_cast<float>((D(static_cast<float>(rest)) * kMsToSec) * speed);
        if (!QuerySwept(b, dist, rest, u)) {
            b.elapsed -= rest;
            done = ms;
            break;
        }
        const float savedSpeed = b.speed;
        const float flat[2] = {u[0], u[1]};
        if (b.flags & 0x200800) {
            float free[3] = {u[0], u[1], u[2]};
            done += FreeMove(b, rest, dist, free);
        } else {
            done += (b.flags & FLAG_FALLING) ? FallStep(b, rest, dist, flat) : Walk(b, rest, dist, flat);
        }
        if (!(b.speed == savedSpeed)) {
            RefreshSpeed(b, false);
            break;
        }
        if (b.elapsed == 0 && (b.flags & 0xF))
            b.elapsed = ms - done;
        if (done >= ms)
            break;
    }
    return done;
}

// The position check (0x00616BF0).
bool PositionValid(const Body &b) {
    for (float c : b.position)
        if (!std::isfinite(c))
            return false;
    const double y = D(kCellCentre) - b.position[1];
    if (y < 2.0 || !(y < kCellSpan))
        return false;
    const float x = static_cast<float>(D(kCellCentre) - b.position[0]);
    return !(x < 2.0f) && x < kCellSpan;
}

// The substep apply (0x00616CB0): the anchor plus the displacement, less
// the position; no vertical part on the ground or in the air.
uint32_t Apply(Body &b, uint32_t r, const float (&delta)[3]) {
    const double tx = D(b.anchor[0]) + delta[0];
    const double ty = D(b.anchor[1]) + delta[1];
    const float tz = static_cast<float>(D(b.anchor[2]) + delta[2]);
    float d[3] = {static_cast<float>(tx - b.position[0]), static_cast<float>(ty - b.position[1]),
                  static_cast<float>(D(tz) - b.position[2])};
    if (!(b.flags & 0x200800))
        d[2] = 0.0f;
    return Resolve(b, r, d);
}

// The substeps of one step (0x00616DE0).
void Integrate(Body &b, uint32_t length) {
    if (!PositionValid(b) || length == 0)
        return;
    uint32_t done = 0;
    for (int pass = 0; pass < kMaxPasses && (b.flags & 0x20FF); ++pass) {
        const uint32_t r = length - done;
        b.elapsed += r;
        float delta[3] = {0.0f, 0.0f, 0.0f};
        if (!Displacement(b, b.elapsed, delta))
            return;
        done += Apply(b, r, delta);
        if (done >= length)
            break;
    }
}

} // namespace

void Init(Body &body, const float (&p)[3], float facing, uint32_t now) {
    body = Body{};
    std::memcpy(body.position, p, sizeof body.position);
    std::memcpy(body.anchor, p, sizeof body.anchor);
    body.facing = body.anchorFacing = facing;
    body.pitchBasis[0] = 1.0f;
    body.fallStartZ = p[2];
    body.walkSpeed = kWalkSpeed;
    body.runSpeed = kRunSpeed;
    body.runBackSpeed = kRunBackSpeed;
    body.swimSpeed = kSwimSpeed;
    body.swimBackSpeed = kSwimBackSpeed;
    body.turnRate = kTurnRate;
    body.radius = kDefaultRadius;
    body.height = kDefaultHeight;
    body.stepRatio = 1.0f;
    body.clock = now;
    Direction(body, false);
}

void SetCollisionSize(Body &body, float width, float height, float scale, float displayScale) {
    body.stepRatio = std::fabs(displayScale) < kParallel ? 1.0f : static_cast<float>(D(scale) / displayScale);
    body.radius = static_cast<float>(D(width) * scale * kHalf);
    body.height = static_cast<float>(D(height) * scale);
}

bool Tick(Body &body, uint32_t now) {
    if (static_cast<int32_t>(now - body.clock) <= 0)
        return false;
    uint32_t window = now - body.clock;
    body.clock = now;
    if (window > kMaxWindowMs)
        window = kMaxWindowMs; // the client reports the rest as skipped (0x00615AE0)
    if (body.flags & 0x20FF)
        Integrate(body, window);
    return true;
}

void Reset(Body &body, const float (&p)[3], float facing) {
    std::memcpy(body.position, p, sizeof body.position);
    body.facing = facing;
    body.pitch = 0.0f;
    body.flags &= 0xFBC07F00;
    Land(body);
    Anchor(body, false);
    RefreshSpeed(body, false);
}

void SetFacing(Body &body, float facing) {
    if (!(std::fabs(D(facing) - body.facing) < kTiny)) {
        body.facing = facing;
        if (!(body.flags & FLAG_FALLING))
            Anchor(body, false);
    }
    body.flags &= ~0x30u;
}

bool FallGate(Body &body) {
    if (body.flags & 0x203800)
        return false;
    StartFall(body, 0.0f);
    return true;
}

bool StartMove(Body &body, bool forward) {
    body.flags &= 0xFFF97FFF;
    const bool falling = (body.flags & FLAG_FALLING) != 0;
    if (falling && (body.flags & 0xF)) {
        if (forward) {
            if (body.flags & FLAG_FORWARD)
                return false;
            body.flags |= 0x20000;
        } else {
            if (body.flags & FLAG_BACKWARD)
                return false;
            body.flags |= 0x40000;
        }
        return false;
    }
    body.flags = forward ? (body.flags & ~FLAG_BACKWARD) | FLAG_FORWARD : (body.flags & ~FLAG_FORWARD) | FLAG_BACKWARD;
    Anchor(body, falling);
    RefreshSpeed(body, falling);
    return true;
}

bool StopMove(Body &body) {
    if (!(body.flags & 0x3)) {
        body.flags &= 0xFFF9FFFF;
        return false;
    }
    if (body.flags & FLAG_ON_STEP) {
        body.flags &= ~FLAG_ON_STEP;
        FallGate(body);
    }
    if (body.flags & FLAG_FALLING) {
        body.flags = (body.flags & 0xFFF9FFFF) | 0x8000;
        return false;
    }
    StopMoveNow(body);
    return true;
}

bool StartStrafe(Body &body, bool left) {
    body.flags &= 0xFFE6FFFF;
    const bool falling = (body.flags & FLAG_FALLING) != 0;
    if (falling && (body.flags & 0xF)) {
        if (left) {
            if (body.flags & FLAG_STRAFE_LEFT)
                return false;
            body.flags |= 0x80000;
        } else {
            if (body.flags & FLAG_STRAFE_RIGHT)
                return false;
            body.flags |= 0x100000;
        }
        return false;
    }
    body.flags = left ? (body.flags & ~FLAG_STRAFE_RIGHT) | FLAG_STRAFE_LEFT
                      : (body.flags & ~FLAG_STRAFE_LEFT) | FLAG_STRAFE_RIGHT;
    Anchor(body, falling);
    RefreshSpeed(body, falling);
    return true;
}

bool StopStrafe(Body &body) {
    if (!(body.flags & 0xC)) {
        body.flags &= 0xFFE7FFFF;
        return false;
    }
    if (body.flags & FLAG_ON_STEP) {
        body.flags &= ~FLAG_ON_STEP;
        FallGate(body);
    }
    if (body.flags & FLAG_FALLING) {
        body.flags = (body.flags & 0xFFE7FFFF) | 0x10000;
        return false;
    }
    StopStrafeNow(body);
    return true;
}

void StartTurn(Body &body, bool left) {
    body.flags = left ? (body.flags & ~FLAG_TURN_RIGHT) | FLAG_TURN_LEFT : (body.flags & ~FLAG_TURN_LEFT) | FLAG_TURN_RIGHT;
    Anchor(body, false);
}

bool StopTurn(Body &body) {
    if (!(body.flags & 0x30))
        return false;
    body.flags &= ~0x30u;
    Anchor(body, false);
    return true;
}

bool Jump(Body &body) {
    if (body.flags & 0x40000000)
        return false;
    if (body.flags & 0x3000)
        return false;
    StartFall(body, (body.flags & FLAG_SWIMMING) ? kSwimJumpSpeed : kJumpSpeed);
    return true;
}

void StartPitch(Body &body, bool up) {
    if (!(body.flags & FLAG_SWIMMING))
        return;
    body.flags = up ? (body.flags & ~FLAG_PITCH_DOWN) | FLAG_PITCH_UP : (body.flags & ~FLAG_PITCH_UP) | FLAG_PITCH_DOWN;
    Anchor(body, false);
}

bool StopPitch(Body &body) {
    if (!(body.flags & 0xC0))
        return false;
    body.flags &= ~0xC0u;
    Anchor(body, false);
    return true;
}

namespace {

// IsRising (0x007C5DE0): in the air, still short of the jump's apex.
bool Rising(const Body &b) {
    if (!(b.flags & FLAG_FALLING) || b.fallSpeed == 0.0f)
        return false;
    const float t = static_cast<float>(static_cast<double>(b.fallTime) * kMsToSec);
    return ApexTime(b) > t;
}

// Swimming on (0x007C6E50): a landing in the water, at the swim speed.
void StartSwimming(Body &b) {
    b.flags = (b.flags & ~FLAG_ON_STEP) | FLAG_SWIMMING;
    Anchor(b, false);
    Land(b);
    RefreshSpeed(b, false);
}

// Swimming off (0x007C6E80): level, then a fall from rest.
void StopSwimming(Body &b) {
    b.flags &= 0xFFDFFF3F;
    b.anchorPitch = 0.0f;
    b.pitch = 0.0f;
    FallGate(b);
    RefreshSpeed(b, false);
}

} // namespace

void UpdateSwimState(Body &body, bool inLiquid, float depth) {
    if (body.flags & 0x400)
        return;
    const double enter = D(body.height) * kThreeQuarters;
    const float leave = static_cast<float>(enter - kThirtySixth);
    if (!(body.flags & FLAG_SWIMMING)) {
        // The active mover's start request (0x00619FB0) is applied as
        // EnterSwim (0x0061A230): swimming on, then the latched directions.
        if (enter < depth && !Rising(body)) {
            StartSwimming(body);
            ApplyLatches(body);
        }
        return;
    }
    if (!inLiquid || !(depth >= leave))
        StopSwimming(body);
}

float ModelPitch(const Body &body) {
    if (!(body.flags & FLAG_SWIMMING) || !(body.flags & 0x3))
        return 0.0f;
    return static_cast<float>(D(kTwoPi) - body.pitch); // 2π at 0x0080295C
}

float FallSpeed(const Body &body) {
    const double cap = Terminal(body);
    const float t = static_cast<float>(D(static_cast<float>(body.fallTime)) * kMsToSec);
    const float w = static_cast<float>(D(t) * kG + CappedFallSpeed(body));
    return cap < w ? static_cast<float>(cap) : w;
}

} // namespace Model::Scene::Mover
