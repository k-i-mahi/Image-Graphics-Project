#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include "Geometry.h"
#include "Shader.h"
#include "Kinematics.h"
#include "Lighting.h"

class TrafficSystem;

// ---------------------------------------------------------------------------
// Street grid. Road centre lines run along x and z at -80, -40, 0, 40, 80
// (junction index -2..2). The shader (scene.frag) uses the same numbers to
// paint lanes, zebra crossings and stop lines from world position.
// ---------------------------------------------------------------------------
namespace Town {
    constexpr float GRID      = 40.0f;   // block pitch (road centre to road centre)
    constexpr int   HALF_N    = 2;       // junction indices -2..2
    constexpr float EDGE      = 80.0f;   // outermost road centre line
    constexpr float ROAD_HALF = 4.5f;    // carriageway half width (two lanes)
    constexpr float LANE      = 2.0f;    // lane centre offset (traffic keeps LEFT, as in Bangladesh)
    constexpr float WALK      = 6.2f;    // pedestrian path offset from the road centre (on the footpath)
    constexpr float KERB_H    = 0.15f;   // footpath height
    constexpr float LOT_HALF  = 12.0f;   // half size of the buildable lot inside a block
    constexpr float HIGHWAY   = 300.0f;  // the two main roads continue out of town to here

    inline float coord(int i) { return i * GRID; }
    // Left of a horizontal travel direction (y up): left-hand traffic drives on this side
    inline glm::vec3 leftOf(const glm::vec3& d) { return glm::vec3(d.z, 0.0f, -d.x); }
}

// Material for one draw. Colours are sRGB; the shader converts to linear.
struct Mat {
    glm::vec3 diffuse   = glm::vec3(0.5f);
    glm::vec3 specular  = glm::vec3(0.2f);
    float     shininess = 16.0f;
    glm::vec3 emissive  = glm::vec3(0.0f);
    int       pattern   = 0;                       // SurfacePattern
    glm::vec2 cell      = glm::vec2(2.6f, 3.4f);   // window grid for PAT_FACADE
    bool      lamp      = false;                   // emissive scaled by lamp glow (dim in daylight)

    Mat() = default;
    Mat(glm::vec3 d, glm::vec3 s = glm::vec3(0.2f), float sh = 16.0f, int pat = 0)
        : diffuse(d), specular(s), shininess(sh), pattern(pat) {}
    bool operator==(const Mat& o) const {
        return diffuse == o.diffuse && specular == o.specular && shininess == o.shininess &&
               emissive == o.emissive && pattern == o.pattern && cell == o.cell && lamp == o.lamp;
    }
};

// Procedural surface patterns understood by scene.frag (uMatPattern)
enum SurfacePattern {
    PAT_NONE = 0, PAT_GROUND = 1, PAT_CONCRETE = 2, PAT_BRICK = 3, PAT_METAL = 4, PAT_FACADE = 5,
    PAT_FOLIAGE = 6, PAT_BARK = 7, PAT_ROAD = 8, PAT_PAVEMENT = 9, PAT_GRASS = 10, PAT_PLAZA = 11,
    PAT_WATER = 12, PAT_ROOFTILE = 13, PAT_GLASS = 14, PAT_PADDY = 15
};

enum MeshKind { MESH_CUBE, MESH_CYLINDER, MESH_SPHERE, MESH_PRISM, MESH_CONE };

// Thin wrapper that sets material uniforms (skipping repeats) and draws unit meshes.
class Painter {
public:
    Painter(const Shader& s, const GeometryManager& g, float lampGlow, bool shadowPass = false)
        : sh(s), geo(g), glow(lampGlow), shadow(shadowPass) {}
    void mat(const Mat& m);
    void draw(MeshKind kind, const glm::mat4& model);
    void box(const glm::vec3& center, const glm::vec3& size);
    void box(const glm::mat4& parent, const glm::vec3& center, const glm::vec3& size);
    void cyl(const glm::mat4& parent, const glm::vec3& center, float radius, float height);
    void sphere(const glm::mat4& parent, const glm::vec3& center, const glm::vec3& size);

private:
    const Shader& sh;
    const GeometryManager& geo;
    float glow;
    bool shadow;
    Mat last;
    bool hasLast = false;
};

// Everything that does not move: roads, footpaths, buildings, parks, lamps, trees.
// Built once into a list of primitives, drawn every frame.
class TownScene {
public:
    ShadingModel currentShadingModel = SHADING_PHONG;
    bool drawBezierGuide = false;
    float lampGlow = 1.0f;
    glm::vec3 cullCenter{ 0.0f };   // people / vehicles far from here are skipped (lost in fog anyway)

    void init(const GeometryManager& geo);
    void render(const Shader& shader, const GeometryManager& geo, const CCTVKinematicChain& cctv,
                const TrafficSystem& traffic, float time, bool shadowPass) const;

    const std::vector<glm::vec3>& lampPositions() const { return lamps; }
    size_t primitiveCount() const { return staticPrimCount; }
    size_t batchCount() const { return batches.size(); }

private:
    struct Prim { MeshKind mesh; glm::mat4 model; Mat mat; };
    std::vector<Prim> prims;          // static scene, before batching
    // Static batching: every primitive sharing a material is merged into one
    // pre-transformed mesh, so the whole static town is a few hundred draws.
    std::vector<std::unique_ptr<Mesh>> batches;
    std::vector<Mat> batchMats;
    size_t staticPrimCount = 0;
    void buildBatches(const GeometryManager& geo);
    std::vector<glm::vec3> lamps;     // street lamp bulb positions (point lights)
    unsigned int seed = 20260923u;

    float rnd();                                     // deterministic 0..1
    void add(MeshKind k, const glm::mat4& m, const Mat& mat) { prims.push_back({ k, m, mat }); }
    void addBox(const glm::vec3& c, const glm::vec3& s, const Mat& mat, float yawDeg = 0.0f);
    void addCyl(const glm::vec3& c, float r, float h, const Mat& mat);
    void addSphere(const glm::vec3& c, const glm::vec3& s, const Mat& mat);
    void addRoof(const glm::vec3& c, const glm::vec3& s, const Mat& mat, float yawDeg);
    void addTree(const glm::vec3& p, float scale, bool conifer);
    void addLamp(const glm::vec3& base, const glm::vec3& towardRoad);
    void addBench(const glm::vec3& p, float yawDeg);
    void addParkedCar(const glm::vec3& p, float yawDeg, const glm::vec3& color);
    void addBeam(const glm::vec3& a, const glm::vec3& b, float width, float thick, const Mat& mat);  // box from a to b
    void addRod(const glm::vec3& a, const glm::vec3& b, float radius, const Mat& mat);               // cylinder from a to b
    void addPalm(const glm::vec3& p, float height, float leanDeg);
    void addPowerLines();
    void addBillboard(const glm::vec3& base, float yawDeg, int design);
    void addTeaStall(const glm::vec3& p, float yawDeg);
    void addPond(const glm::vec3& c);

    void buildRoads();
    void buildBlock(int bi, int bj);
    void buildOutskirts();

    // Block types
    void blockTowers(const glm::vec3& c);
    void blockMosque(const glm::vec3& c);
    void blockPark(const glm::vec3& c);
    void blockShops(const glm::vec3& c, int variant);
    void blockApartments(const glm::vec3& c);
    void blockHouses(const glm::vec3& c);
    void blockSchool(const glm::vec3& c);
    void blockPolice(const glm::vec3& c);
    void blockBusTerminal(const glm::vec3& c);
    void blockFuel(const glm::vec3& c);

    // Moving / animated parts (drawn every frame)
    void renderSignals(Painter& p, const TrafficSystem& traffic) const;
    void renderVehicles(Painter& p, const TrafficSystem& traffic, float time) const;
    void renderPedestrians(Painter& p, const TrafficSystem& traffic) const;
    void renderCCTV(Painter& p, const CCTVKinematicChain& cctv, float time) const;
    void renderFountain(Painter& p, float time) const;
    void renderRouteGuide(Painter& p, const TrafficSystem& traffic) const;
};
