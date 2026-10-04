#pragma once

#include <glm/glm.hpp>
#include <vector>
#include "Bezier.h"

// ---------------------------------------------------------------------------
// Traffic simulation: signals at every junction, vehicles on lane loops with
// cubic Bezier corner turns, and pedestrians walking the footpaths.
//
// A route is a closed polyline sampled from straight lane pieces and cubic
// Bezier turns (P(t) for position, P'(t) for heading). Agents move along it
// by arc length s. "Stop points" on a route mark stop lines / kerbs where the
// agent must wait unless its signal is green.
// ---------------------------------------------------------------------------

enum SignalState { SIG_GREEN = 0, SIG_YELLOW = 1, SIG_RED = 2 };
enum Axis { AXIS_X = 0, AXIS_Z = 1 };

struct StopPoint {
    float s;        // arc length of the stop line / kerb on the route
    int gi, gj;     // junction indices
    int axis;       // travel axis when crossing it
};

struct Route {
    std::vector<glm::vec3> pts;     // dense samples (closed loop: last joins first)
    std::vector<float> cum;         // arc length at each sample
    float length = 0.0f;
    std::vector<StopPoint> stops;   // sorted by s
    std::vector<BezierSegment> turns;  // corner curves (for the G route guide)

    void addPoint(const glm::vec3& p);
    void close();
    glm::vec3 sample(float s, glm::vec3* tangent = nullptr) const;
};

enum VehicleType { VT_CAR, VT_TAXI, VT_POLICE, VT_BUS, VT_TRUCK, VT_CNG, VT_RICKSHAW, VT_PATROL, VT_COUNT };

struct Vehicle {
    VehicleType type;
    int route;
    float s = 0.0f;           // arc length along the route
    float speed = 0.0f;       // m/s
    float maxSpeed = 10.0f;
    float length = 4.4f, width = 1.8f;
    glm::vec3 pos{ 0.0f }, fwd{ 0.0f, 0.0f, 1.0f };
    float yawDeg = 0.0f;      // heading = atan2(P'x, P'z)
    float wheelAngle = 0.0f;  // accumulated roll: delta_s / r
    bool braking = false;
    glm::vec3 color{ 0.6f };
    int nextStop = 0;         // index into route.stops
};

struct Pedestrian {
    int route;
    float s = 0.0f;
    float speed = 0.0f, maxSpeed = 1.3f;
    float lateral = 0.0f;     // sideways offset on the footpath
    float height = 1.0f;      // body scale
    glm::vec3 pos{ 0.0f }, fwd{ 0.0f, 0.0f, 1.0f };
    float yawDeg = 0.0f;
    float phase = 0.0f;       // walk cycle angle (radians)
    glm::vec3 shirt{ 0.5f }, pants{ 0.2f }, skin{ 0.5f }, hair{ 0.05f };
    int style = 0;            // 0 shirt+trousers, 1 long dress (saree / salwar), 2 lungi
    int nextStop = 0;
};

class TrafficSystem {
public:
    float time = 0.0f;
    bool paused = false;
    int patrolVehicle = 0;    // index of the NightWatch patrol car (chase camera, route guide)

    std::vector<Route> routes;
    std::vector<Vehicle> vehicles;
    std::vector<Pedestrian> pedestrians;

    void init();
    void update(float dt);

    // Signal shown to traffic travelling along `axis` at junction (gi, gj)
    SignalState signal(int gi, int gj, int axis) const;
    float cycleTime(int gi, int gj) const;

private:
    unsigned int seed = 777u;
    float rnd();

    int addVehicleRoute(const std::vector<glm::ivec2>& nodes);      // junction / highway nodes
    int addWalkRoute(int i0, int j0, int i1, int j1, bool reverse);  // footpath loop around blocks
    void spawnVehicles(int route, int count, const std::vector<VehicleType>& types);
    void spawnPedestrians(int route, int count);
    void placeVehicle(Vehicle& v) const;
    void placePedestrian(Pedestrian& p) const;
};
