#include "Traffic.h"
#include "Town.h"
#include <algorithm>
#include <cmath>

using namespace Town;

// ---------------------------------------------------------------------------
// Route: dense closed polyline + cumulative arc length
// ---------------------------------------------------------------------------
void Route::addPoint(const glm::vec3& p) {
    if (!pts.empty()) {
        float d = glm::length(p - pts.back());
        if (d < 1e-3f) return;
        cum.push_back(cum.back() + d);
    } else {
        cum.push_back(0.0f);
    }
    pts.push_back(p);
}

void Route::close() {
    addPoint(pts.front());            // last sample == first sample
    length = cum.back();
    std::sort(stops.begin(), stops.end(), [](const StopPoint& a, const StopPoint& b) { return a.s < b.s; });
}

glm::vec3 Route::sample(float s, glm::vec3* tangent) const {
    s = std::fmod(s, length);
    if (s < 0.0f) s += length;
    size_t i = std::upper_bound(cum.begin(), cum.end(), s) - cum.begin();
    i = std::clamp<size_t>(i, 1, pts.size() - 1);
    float seg = cum[i] - cum[i - 1];
    float t = seg > 0.0f ? (s - cum[i - 1]) / seg : 0.0f;
    if (tangent) *tangent = glm::normalize(pts[i] - pts[i - 1]);
    return glm::mix(pts[i - 1], pts[i], t);
}

// ---------------------------------------------------------------------------
// Signals: every junction runs the same 28.5 s cycle, offset per junction
//   X green 11 s -> X yellow 2.5 s -> all red 1 s -> Z green 11 s -> Z yellow 2 s -> all red 1 s
// ---------------------------------------------------------------------------
static const float CYCLE = 28.5f;

float TrafficSystem::cycleTime(int gi, int gj) const {
    float t = time + static_cast<float>((gi + 2) * 5 + (gj + 2) * 3);
    return std::fmod(t, CYCLE);
}

SignalState TrafficSystem::signal(int gi, int gj, int axis) const {
    float t = cycleTime(gi, gj);
    if (axis == AXIS_X) {
        if (t < 11.0f) return SIG_GREEN;
        if (t < 13.5f) return SIG_YELLOW;
        return SIG_RED;
    }
    if (t >= 14.5f && t < 25.5f) return SIG_GREEN;
    if (t >= 25.5f && t < 27.5f) return SIG_YELLOW;
    return SIG_RED;
}

float TrafficSystem::rnd() {
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) / 16777216.0f;
}

static glm::vec3 nodePos(const glm::ivec2& n) { return glm::vec3(coord(n.x), 0.0f, coord(n.y)); }
static bool inTown(int i, int j) { return std::abs(i) <= HALF_N && std::abs(j) <= HALF_N; }

// Samples a cubic Bezier into the route (excluding its first point)
static void addBezier(Route& r, const BezierSegment& b, int samples) {
    for (int k = 1; k <= samples; ++k) r.addPoint(b.evaluate(static_cast<float>(k) / samples));
    r.turns.push_back(b);
}

// ---------------------------------------------------------------------------
// Vehicle route through a closed list of corner nodes (axis-aligned legs).
// At each node the car turns on a cubic Bezier from the incoming lane to the
// outgoing lane; a reversal (highway end) becomes a U-turn.
// ---------------------------------------------------------------------------
int TrafficSystem::addVehicleRoute(const std::vector<glm::ivec2>& nodes) {
    Route r;
    const int n = static_cast<int>(nodes.size());
    std::vector<glm::vec3> entry(n), exitP(n);
    std::vector<BezierSegment> curve;

    for (int k = 0; k < n; ++k) {
        glm::vec3 C = nodePos(nodes[k]);
        glm::vec3 dIn = glm::normalize(C - nodePos(nodes[(k + n - 1) % n]));
        glm::vec3 dOut = glm::normalize(nodePos(nodes[(k + 1) % n]) - C);
        glm::vec3 offIn = leftOf(dIn) * LANE, offOut = leftOf(dOut) * LANE;
        if (glm::dot(dIn, dOut) < -0.9f) {
            // U-turn: swing across from one lane to the other
            entry[k] = C + offIn;
            exitP[k] = C + offOut;
            curve.emplace_back(entry[k], entry[k] + dIn * 3.0f, exitP[k] + dIn * 3.0f, exitP[k]);
        } else {
            // 90 degree turn: corner of the two lane lines, rounded with radius ROAD_HALF
            glm::vec3 Q = C + offIn + offOut;
            const float rad = ROAD_HALF, k4 = 0.5523f * rad;   // cubic approximation of a quarter circle
            entry[k] = Q - dIn * rad;
            exitP[k] = Q + dOut * rad;
            curve.emplace_back(entry[k], entry[k] + dIn * k4, exitP[k] - dOut * k4, exitP[k]);
        }
    }

    for (int k = 0; k < n; ++k) {
        int prev = (k + n - 1) % n;
        glm::vec3 A = exitP[prev], B = entry[k];
        glm::vec3 d = glm::normalize(nodePos(nodes[k]) - nodePos(nodes[prev]));
        float segLen = glm::length(B - A);
        float s0 = r.pts.empty() ? 0.0f : r.cum.back();
        if (r.pts.empty()) r.addPoint(A);
        for (float t = 2.0f; t < segLen; t += 2.0f) r.addPoint(A + d * t);
        r.addPoint(B);

        // Stop lines of every junction this leg drives into (not the one it leaves)
        glm::ivec2 step(static_cast<int>(std::round(d.x)), static_cast<int>(std::round(d.z)));
        for (glm::ivec2 g = nodes[prev] + step; ; g += step) {
            if (inTown(g.x, g.y)) {
                glm::vec3 stopPos = nodePos(g) + leftOf(d) * LANE - d * (ROAD_HALF + 4.7f);
                float along = glm::dot(stopPos - A, d);
                if (along >= 0.0f && along <= segLen + 0.01f)
                    r.stops.push_back({ s0 + along, g.x, g.y, std::fabs(d.x) > 0.5f ? AXIS_X : AXIS_Z });
            }
            if (g == nodes[k]) break;
        }
        addBezier(r, curve[k], 14);
    }
    r.close();
    routes.push_back(r);
    return static_cast<int>(routes.size()) - 1;
}

// ---------------------------------------------------------------------------
// Pedestrian loop on the footpaths inside the rectangle of junctions
// (i0, j0)-(i1, j1). Walking across a road happens on its zebra crossing,
// with a kerb stop that waits for the parallel traffic's green.
// ---------------------------------------------------------------------------
int TrafficSystem::addWalkRoute(int i0, int j0, int i1, int j1, bool reverse) {
    float x0 = coord(i0) + WALK, x1 = coord(i1) - WALK;
    float z0 = coord(j0) + WALK, z1 = coord(j1) - WALK;
    std::vector<glm::vec3> c = { { x0, 0, z0 }, { x1, 0, z0 }, { x1, 0, z1 }, { x0, 0, z1 } };
    if (reverse) std::reverse(c.begin(), c.end());

    Route r;
    const float rc = 1.2f;                     // rounded corner radius
    const int n = 4;
    for (int k = 0; k < n; ++k) {
        glm::vec3 P = c[k], Pn = c[(k + 1) % n], Pnn = c[(k + 2) % n];
        glm::vec3 d = glm::normalize(Pn - P), dNext = glm::normalize(Pnn - Pn);
        glm::vec3 A = P + d * rc, B = Pn - d * rc;
        float s0 = r.pts.empty() ? 0.0f : r.cum.back();
        if (r.pts.empty()) r.addPoint(A);
        float len = glm::length(B - A);
        for (float t = 1.0f; t < len; t += 1.0f) r.addPoint(A + d * t);
        r.addPoint(B);

        // Roads crossed by this side: centre lines strictly between its ends
        bool alongX = std::fabs(d.x) > 0.5f;
        float a = alongX ? P.x : P.z, b = alongX ? Pn.x : Pn.z;
        float fixed = alongX ? P.z : P.x;
        int fixedIdx = static_cast<int>(std::round(fixed / GRID));
        for (int m = -HALF_N; m <= HALF_N; ++m) {
            float line = coord(m);
            if (line <= std::min(a, b) || line >= std::max(a, b)) continue;
            float kerb = line - (b > a ? 1.0f : -1.0f) * (ROAD_HALF + 0.6f);
            float along = std::fabs(kerb - (alongX ? A.x : A.z));
            int gi = alongX ? m : fixedIdx, gj = alongX ? fixedIdx : m;
            r.stops.push_back({ s0 + along, gi, gj, alongX ? AXIS_X : AXIS_Z });
        }
        // rounded corner at Pn
        glm::vec3 e0 = B, e3 = Pn + dNext * rc;
        addBezier(r, BezierSegment(e0, e0 + d * 0.55f * rc, e3 - dNext * 0.55f * rc, e3), 4);
    }
    r.close();
    routes.push_back(r);
    return static_cast<int>(routes.size()) - 1;
}

// ---------------------------------------------------------------------------
// Agents
// ---------------------------------------------------------------------------
static int firstStopAfter(const Route& r, float s) {
    for (size_t i = 0; i < r.stops.size(); ++i)
        if (r.stops[i].s >= s) return static_cast<int>(i);
    return 0;
}

void TrafficSystem::placeVehicle(Vehicle& v) const {
    glm::vec3 t;
    v.pos = routes[v.route].sample(v.s, &t);
    v.fwd = t;
    v.yawDeg = glm::degrees(std::atan2(t.x, t.z));     // heading from the tangent P'(t)
}

void TrafficSystem::placePedestrian(Pedestrian& p) const {
    if (p.route < 0) return;                            // standing still
    glm::vec3 t;
    glm::vec3 c = routes[p.route].sample(p.s, &t);
    p.fwd = t;
    p.pos = c + leftOf(t) * p.lateral;
    p.yawDeg = glm::degrees(std::atan2(t.x, t.z));
}

void TrafficSystem::spawnVehicles(int route, int count, const std::vector<VehicleType>& types) {
    static const glm::vec3 carColors[] = {
        { 0.75f, 0.75f, 0.78f }, { 0.08f, 0.08f, 0.09f }, { 0.62f, 0.08f, 0.07f }, { 0.12f, 0.22f, 0.48f },
        { 0.92f, 0.92f, 0.9f }, { 0.35f, 0.38f, 0.40f }, { 0.55f, 0.45f, 0.30f }, { 0.10f, 0.35f, 0.25f } };
    const Route& r = routes[route];
    for (int i = 0; i < count; ++i) {
        Vehicle v;
        v.type = types[i % types.size()];
        v.route = route;
        v.s = (i + 0.3f * rnd()) * r.length / count;
        switch (v.type) {
            case VT_CAR:      v.length = 4.4f; v.width = 1.8f; v.maxSpeed = 10.5f + 2.0f * rnd(); v.color = carColors[static_cast<int>(rnd() * 8) % 8]; break;
            case VT_TAXI:     v.length = 4.4f; v.width = 1.8f; v.maxSpeed = 11.0f; v.color = glm::vec3(0.95f, 0.75f, 0.05f); break;
            case VT_POLICE:   v.length = 4.6f; v.width = 1.85f; v.maxSpeed = 12.0f; v.color = glm::vec3(0.92f); break;
            case VT_BUS:      v.length = 11.0f; v.width = 2.5f; v.maxSpeed = 8.5f;
                              v.color = rnd() < 0.5f ? glm::vec3(0.75f, 0.10f, 0.08f) : glm::vec3(0.10f, 0.45f, 0.25f); break;
            case VT_TRUCK:    v.length = 7.6f; v.width = 2.4f; v.maxSpeed = 8.0f; v.color = glm::vec3(0.85f, 0.45f, 0.08f); break;
            case VT_CNG:      v.length = 2.7f; v.width = 1.4f; v.maxSpeed = 8.5f; v.color = glm::vec3(0.10f, 0.50f, 0.18f); break;
            case VT_RICKSHAW: v.length = 2.5f; v.width = 1.15f; v.maxSpeed = 3.8f + 0.6f * rnd();
                              v.color = glm::vec3(0.2f + 0.8f * rnd(), 0.2f + 0.5f * rnd(), 0.3f + 0.7f * rnd()); break;
            case VT_PATROL:   v.length = 5.4f; v.width = 2.2f; v.maxSpeed = 9.5f; v.color = glm::vec3(0.35f, 0.48f, 0.75f); break;
            default: break;
        }
        v.speed = v.maxSpeed * 0.5f;
        v.nextStop = firstStopAfter(r, v.s);
        placeVehicle(v);
        if (v.type == VT_PATROL) patrolVehicle = static_cast<int>(vehicles.size());
        vehicles.push_back(v);
    }
}

void TrafficSystem::spawnPedestrians(int route, int count) {
    static const glm::vec3 shirts[] = {
        { 0.85f, 0.85f, 0.82f }, { 0.15f, 0.30f, 0.60f }, { 0.70f, 0.15f, 0.15f }, { 0.20f, 0.50f, 0.30f },
        { 0.90f, 0.70f, 0.20f }, { 0.45f, 0.20f, 0.55f }, { 0.95f, 0.45f, 0.10f }, { 0.20f, 0.20f, 0.22f } };
    static const glm::vec3 dresses[] = {
        { 0.85f, 0.10f, 0.35f }, { 0.95f, 0.55f, 0.05f }, { 0.10f, 0.55f, 0.55f }, { 0.55f, 0.10f, 0.60f },
        { 0.90f, 0.20f, 0.15f }, { 0.20f, 0.60f, 0.25f } };
    for (int i = 0; i < count; ++i) {
        Pedestrian p;
        p.route = route;
        p.s = route >= 0 ? rnd() * routes[route].length : 0.0f;
        p.maxSpeed = 1.0f + 0.6f * rnd();
        p.speed = p.maxSpeed;
        p.lateral = (rnd() - 0.5f) * 1.4f;
        p.height = 0.9f + 0.18f * rnd();
        p.phase = rnd() * 6.28f;
        float tone = rnd();
        p.skin = glm::mix(glm::vec3(0.36f, 0.22f, 0.14f), glm::vec3(0.72f, 0.52f, 0.38f), tone);
        p.hair = glm::vec3(0.03f, 0.025f, 0.02f) + glm::vec3(0.08f) * rnd() * (rnd() < 0.15f ? 4.0f : 1.0f);
        float r = rnd();
        p.style = r < 0.3f ? 1 : (r < 0.45f ? 2 : 0);
        if (p.style == 1) {
            p.shirt = dresses[static_cast<int>(rnd() * 6) % 6];
            p.pants = p.shirt * 0.8f;
        } else {
            p.shirt = shirts[static_cast<int>(rnd() * 8) % 8];
            p.pants = p.style == 2 ? glm::vec3(0.15f + 0.3f * rnd(), 0.25f + 0.2f * rnd(), 0.35f + 0.3f * rnd())
                                   : glm::mix(glm::vec3(0.08f, 0.09f, 0.14f), glm::vec3(0.45f, 0.40f, 0.32f), rnd());
        }
        if (route >= 0) p.nextStop = firstStopAfter(routes[route], p.s);
        placePedestrian(p);
        pedestrians.push_back(p);
    }
}

void TrafficSystem::init() {
    routes.clear();
    vehicles.clear();
    pedestrians.clear();

    using V = std::vector<glm::ivec2>;
    // --- vehicle routes (nodes = junction indices; |7| = end of the highway) ---
    int perimeter  = addVehicleRoute(V{ { -2, -2 }, { 2, -2 }, { 2, 2 }, { -2, 2 } });
    int perimRev   = addVehicleRoute(V{ { -2, -2 }, { -2, 2 }, { 2, 2 }, { 2, -2 } });
    int inner      = addVehicleRoute(V{ { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } });
    int innerRev   = addVehicleRoute(V{ { -1, -1 }, { -1, 1 }, { 1, 1 }, { 1, -1 } });
    int centreSE   = addVehicleRoute(V{ { 0, 0 }, { 2, 0 }, { 2, 2 }, { 0, 2 } });
    int centreNW   = addVehicleRoute(V{ { -2, -2 }, { 0, -2 }, { 0, 0 }, { -2, 0 } });
    int highwayE   = addVehicleRoute(V{ { 7, 0 }, { 1, 0 }, { 1, 1 }, { 2, 1 }, { 2, 0 } });
    int highwayW   = addVehicleRoute(V{ { -7, 0 }, { -1, 0 }, { -1, -1 }, { -2, -1 }, { -2, 0 } });
    int highwayN   = addVehicleRoute(V{ { 0, -7 }, { 0, -1 }, { 1, -1 }, { 1, -2 }, { 0, -2 } });
    int highwayS   = addVehicleRoute(V{ { 0, 7 }, { 0, 1 }, { -1, 1 }, { -1, 2 }, { 0, 2 } });
    int westLoop   = addVehicleRoute(V{ { -2, 0 }, { 0, 0 }, { 0, 2 }, { -2, 2 } });
    int ring       = addVehicleRoute(V{ { -3, -3 }, { 3, -3 }, { 3, 3 }, { -3, 3 } });       // outer districts
    int ringRev    = addVehicleRoute(V{ { -3, -3 }, { -3, 3 }, { 3, 3 }, { 3, -3 } });
    int northEast  = addVehicleRoute(V{ { 1, -3 }, { 3, -3 }, { 3, -1 }, { 1, -1 } });
    int southWest  = addVehicleRoute(V{ { -3, 1 }, { -1, 1 }, { -1, 3 }, { -3, 3 } });

    spawnVehicles(perimeter, 7, { VT_PATROL, VT_CAR, VT_BUS, VT_CAR, VT_TRUCK, VT_CAR, VT_CNG });
    spawnVehicles(perimRev, 5, { VT_BUS, VT_CAR, VT_CNG, VT_CAR, VT_TAXI });
    spawnVehicles(inner, 6, { VT_CAR, VT_RICKSHAW, VT_TAXI, VT_CNG, VT_CAR, VT_CAR });
    spawnVehicles(innerRev, 5, { VT_POLICE, VT_CAR, VT_CNG, VT_CAR, VT_RICKSHAW });
    spawnVehicles(centreSE, 4, { VT_CAR, VT_RICKSHAW, VT_CNG, VT_TAXI });
    spawnVehicles(centreNW, 4, { VT_CAR, VT_CNG, VT_RICKSHAW, VT_CAR });
    spawnVehicles(highwayE, 4, { VT_CAR, VT_TRUCK, VT_CAR, VT_BUS });
    spawnVehicles(highwayW, 4, { VT_CAR, VT_BUS, VT_CAR, VT_CNG });
    spawnVehicles(highwayN, 3, { VT_TRUCK, VT_CAR, VT_TAXI });
    spawnVehicles(highwayS, 4, { VT_CAR, VT_CNG, VT_CAR, VT_TRUCK });
    spawnVehicles(westLoop, 3, { VT_RICKSHAW, VT_CAR, VT_CNG });
    spawnVehicles(ring, 8, { VT_TRUCK, VT_CAR, VT_BUS, VT_CNG, VT_CAR, VT_TAXI, VT_CAR, VT_RICKSHAW });
    spawnVehicles(ringRev, 6, { VT_CAR, VT_CNG, VT_TRUCK, VT_CAR, VT_RICKSHAW, VT_BUS });
    spawnVehicles(northEast, 3, { VT_TRUCK, VT_CAR, VT_CNG });
    spawnVehicles(southWest, 3, { VT_RICKSHAW, VT_CAR, VT_CNG });

    // --- pedestrian routes: every block, plus loops that cross streets ---
    for (int bi = -HALF_N; bi < HALF_N; ++bi)
        for (int bj = -HALF_N; bj < HALF_N; ++bj) {
            spawnPedestrians(addWalkRoute(bi, bj, bi + 1, bj + 1, false), 2);
            spawnPedestrians(addWalkRoute(bi, bj, bi + 1, bj + 1, true), 1);
        }
    spawnPedestrians(addWalkRoute(-1, -1, 1, 1, false), 6);
    spawnPedestrians(addWalkRoute(-1, -1, 1, 1, true), 5);
    spawnPedestrians(addWalkRoute(0, 0, 2, 2, false), 4);
    spawnPedestrians(addWalkRoute(-2, 0, 0, 2, true), 4);
    spawnPedestrians(addWalkRoute(0, -2, 2, 0, false), 3);
    spawnPedestrians(addWalkRoute(-2, -2, 0, 0, true), 3);

    // --- shoppers browsing the two bazaars ---
    for (const glm::vec2 bz : { glm::vec2(-20.0f, -100.0f), glm::vec2(-100.0f, 20.0f) })
        for (int i = 0; i < 8; ++i) {
            spawnPedestrians(-1, 1);
            Pedestrian& p = pedestrians.back();
            float lane = -6.0f + (i % 3) * 6.0f;
            p.pos = glm::vec3(bz.x + lane + (rnd() - 0.5f) * 1.2f, KERB_H, bz.y - 9.0f + rnd() * 18.0f);
            p.yawDeg = rnd() * 360.0f;
            p.fwd = glm::vec3(std::sin(glm::radians(p.yawDeg)), 0.0f, std::cos(glm::radians(p.yawDeg)));
            p.speed = p.maxSpeed = 0.0f;
        }

    // --- people standing still: park, bus terminal, market fronts ---
    const glm::vec4 idle[] = {   // x, z, facing (deg), unused
        { -24, 15, 30, 0 }, { -23, 16.5f, 200, 0 }, { -14, 28, 90, 0 }, { -13, 29, 260, 0 },
        { -30, 30, 120, 0 }, { 26, 52, 0, 0 }, { 27.5f, 52, 10, 0 }, { 29, 52.5f, 350, 0 },
        { 12, 13, 180, 0 }, { 13, 13.4f, 170, 0 }, { -47, 13, 180, 0 }, { -60, 13.2f, 160, 0 } };
    for (const auto& q : idle) {
        spawnPedestrians(-1, 1);
        Pedestrian& p = pedestrians.back();
        p.pos = glm::vec3(q.x, KERB_H, q.y);
        p.yawDeg = q.z;
        p.fwd = glm::vec3(std::sin(glm::radians(q.z)), 0.0f, std::cos(glm::radians(q.z)));
        p.speed = 0.0f;
        p.maxSpeed = 0.0f;
    }
}

// ---------------------------------------------------------------------------
// Simulation step
// ---------------------------------------------------------------------------
// Signed distance from a to b along a closed loop of length L, in (-L/2, L/2]
static float loopDelta(float a, float b, float L) {
    float d = std::fmod(b - a, L);
    if (d > L * 0.5f) d -= L;
    if (d <= -L * 0.5f) d += L;
    return d;
}

void TrafficSystem::update(float dt) {
    if (paused || dt <= 0.0f) return;
    dt = std::min(dt, 0.05f);
    time += dt;

    // ---- vehicles ----
    for (size_t i = 0; i < vehicles.size(); ++i) {
        Vehicle& v = vehicles[i];
        const Route& r = routes[v.route];
        float target = v.maxSpeed;
        float front = v.s + v.length * 0.5f;

        // 1) traffic signals: the next stop line ahead
        if (!r.stops.empty()) {
            for (size_t guard = 0; guard < r.stops.size(); ++guard) {
                if (loopDelta(front, r.stops[v.nextStop].s, r.length) > -1.0f) break;
                v.nextStop = (v.nextStop + 1) % static_cast<int>(r.stops.size());
            }
            const StopPoint& sp = r.stops[v.nextStop];
            float ds = loopDelta(front, sp.s, r.length);
            if (ds > -1.0f && ds < 45.0f) {
                SignalState sg = signal(sp.gi, sp.gj, sp.axis);
                float brakeDist = v.speed * v.speed / (2.0f * 4.0f);
                bool stop = (sg == SIG_RED && ds > brakeDist * 0.5f - 0.5f) || (sg == SIG_YELLOW && ds > brakeDist + 1.0f);
                if (stop) target = std::min(target, std::sqrt(2.0f * 3.0f * std::max(0.0f, ds - 0.3f)));
            }
        }

        // 2) the vehicle ahead in the same lane
        glm::vec3 left = leftOf(v.fwd);
        for (size_t k = 0; k < vehicles.size(); ++k) {
            if (k == i) continue;
            const Vehicle& u = vehicles[k];
            glm::vec3 rel = u.pos - v.pos;
            float along = glm::dot(rel, v.fwd);
            if (along <= 0.0f || along > 35.0f) continue;
            if (std::fabs(glm::dot(rel, left)) > 1.7f) continue;
            if (glm::dot(u.fwd, v.fwd) < 0.2f) continue;
            float gap = along - (u.length + v.length) * 0.5f;
            target = std::min(target, std::max(0.0f, u.speed + (gap - 2.5f) * 0.8f));
        }

        // 3) pedestrians on the road in front
        for (const Pedestrian& p : pedestrians) {
            glm::vec3 rel = p.pos - v.pos;
            float along = glm::dot(rel, v.fwd);
            if (along <= 0.0f || along > 14.0f) continue;
            if (std::fabs(glm::dot(rel, left)) > 1.6f) continue;
            float gap = along - v.length * 0.5f;
            target = std::min(target, std::max(0.0f, (gap - 2.0f) * 1.2f));
        }

        float accel = (v.type == VT_BUS || v.type == VT_TRUCK) ? 1.5f : 2.4f;
        float dv = glm::clamp(target - v.speed, -7.0f * dt, accel * dt);
        v.braking = dv < -0.5f * dt || v.speed < 0.2f;
        v.speed = std::max(0.0f, v.speed + dv);
        float ds = v.speed * dt;
        v.s = std::fmod(v.s + ds, r.length);
        float wheelR = (v.type == VT_BUS || v.type == VT_TRUCK) ? 0.5f : (v.type == VT_RICKSHAW ? 0.35f : 0.33f);
        v.wheelAngle = std::fmod(v.wheelAngle + glm::degrees(ds / wheelR), 360.0f);   // rolling: angle = s / r
        placeVehicle(v);
    }

    // ---- pedestrians ----
    for (Pedestrian& p : pedestrians) {
        if (p.route < 0) continue;
        const Route& r = routes[p.route];
        float target = p.maxSpeed;
        if (!r.stops.empty()) {
            for (size_t guard = 0; guard < r.stops.size(); ++guard) {
                if (loopDelta(p.s, r.stops[p.nextStop].s, r.length) > -0.5f) break;
                p.nextStop = (p.nextStop + 1) % static_cast<int>(r.stops.size());
            }
            const StopPoint& sp = r.stops[p.nextStop];
            float ds = loopDelta(p.s, sp.s, r.length);
            // cross only on the parallel green, and not in its last 3 seconds
            bool go = signal(sp.gi, sp.gj, sp.axis) == SIG_GREEN;
            float t = cycleTime(sp.gi, sp.gj);
            if (go && ((sp.axis == AXIS_X && t > 8.0f) || (sp.axis == AXIS_Z && t > 22.5f))) go = false;
            if (!go && ds > -0.5f && ds < 3.0f) target = std::min(target, std::max(0.0f, ds * 1.5f));
        }
        p.speed += glm::clamp(target - p.speed, -3.0f * dt, 1.5f * dt);
        p.speed = std::max(0.0f, p.speed);
        p.s = std::fmod(p.s + p.speed * dt, r.length);
        p.phase += p.speed * dt * 6.2831853f / (1.5f * p.height);   // one stride cycle per ~1.5 m
        placePedestrian(p);
    }
}
