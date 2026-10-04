#include "Town.h"
#include "Traffic.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace Town;

// ---------------------------------------------------------------------------
// Painter
// ---------------------------------------------------------------------------
void Painter::mat(const Mat& m) {
    if (shadow) return;                       // depth-only pass: no material uniforms
    if (hasLast && m == last) return;         // consecutive draws often share a material
    sh.setVec3("uMatDiffuse", m.diffuse);
    sh.setVec3("uMatSpecular", m.specular);
    sh.setFloat("uMatShininess", m.shininess);
    sh.setVec3("uMatEmissive", m.lamp ? m.emissive * glow : m.emissive);
    sh.setInt("uMatPattern", m.pattern);
    if (m.pattern == PAT_FACADE) sh.setVec2("uFacadeCell", m.cell);
    last = m;
    hasLast = true;
}

void Painter::draw(MeshKind kind, const glm::mat4& model) {
    sh.setMat4("uModel", model);
    switch (kind) {
        case MESH_CUBE:     geo.drawCube(); break;
        case MESH_CYLINDER: geo.drawCylinder(); break;
        case MESH_SPHERE:   geo.drawSphere(); break;
        case MESH_PRISM:    geo.drawPrism(); break;
        case MESH_CONE:     geo.drawCone(); break;
    }
}

void Painter::box(const glm::vec3& c, const glm::vec3& s) {
    draw(MESH_CUBE, glm::scale(glm::translate(glm::mat4(1.0f), c), s));
}

void Painter::box(const glm::mat4& parent, const glm::vec3& c, const glm::vec3& s) {
    draw(MESH_CUBE, glm::scale(glm::translate(parent, c), s));
}

void Painter::cyl(const glm::mat4& parent, const glm::vec3& c, float r, float h) {
    draw(MESH_CYLINDER, glm::scale(glm::translate(parent, c), glm::vec3(2.0f * r, h, 2.0f * r)));
}

void Painter::sphere(const glm::mat4& parent, const glm::vec3& c, const glm::vec3& s) {
    draw(MESH_SPHERE, glm::scale(glm::translate(parent, c), s));
}

// ---------------------------------------------------------------------------
// Static scene construction helpers
// ---------------------------------------------------------------------------
float TownScene::rnd() {
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) / 16777216.0f;
}

static Mat mat(glm::vec3 d, int pattern = PAT_NONE, float spec = 0.15f, float shin = 16.0f) {
    return Mat(d, glm::vec3(spec), shin, pattern);
}
static Mat facade(glm::vec3 d, glm::vec2 cell) {
    Mat m(d, glm::vec3(0.2f), 24.0f, PAT_FACADE);
    m.cell = cell;
    return m;
}
static Mat glow(glm::vec3 d, glm::vec3 e, bool lamp = true) {
    Mat m(d, glm::vec3(0.5f), 32.0f, PAT_NONE);
    m.emissive = e;
    m.lamp = lamp;
    return m;
}

void TownScene::addBox(const glm::vec3& c, const glm::vec3& s, const Mat& m, float yawDeg) {
    glm::mat4 t = glm::translate(glm::mat4(1.0f), c);
    if (yawDeg != 0.0f) t = glm::rotate(t, glm::radians(yawDeg), glm::vec3(0, 1, 0));
    add(MESH_CUBE, glm::scale(t, s), m);
}

void TownScene::addCyl(const glm::vec3& c, float r, float h, const Mat& m) {
    add(MESH_CYLINDER, glm::scale(glm::translate(glm::mat4(1.0f), c), glm::vec3(2 * r, h, 2 * r)), m);
}

void TownScene::addSphere(const glm::vec3& c, const glm::vec3& s, const Mat& m) {
    add(MESH_SPHERE, glm::scale(glm::translate(glm::mat4(1.0f), c), s), m);
}

void TownScene::addRoof(const glm::vec3& c, const glm::vec3& s, const Mat& m, float yawDeg) {
    glm::mat4 t = glm::rotate(glm::translate(glm::mat4(1.0f), c), glm::radians(yawDeg), glm::vec3(0, 1, 0));
    add(MESH_PRISM, glm::scale(t, s), m);
}

void TownScene::addTree(const glm::vec3& p, float s, bool conifer) {
    float h = (2.6f + rnd() * 1.6f) * s;
    addCyl(p + glm::vec3(0, h * 0.5f, 0), 0.18f * s, h, mat({ 0.30f, 0.21f, 0.13f }, PAT_BARK, 0.05f, 4));
    glm::vec3 leaf = glm::mix(glm::vec3(0.16f, 0.32f, 0.12f), glm::vec3(0.30f, 0.42f, 0.14f), rnd());
    Mat lm = mat(leaf, PAT_FOLIAGE, 0.1f, 8);
    if (conifer) {
        for (int i = 0; i < 3; ++i) {
            float r = (1.9f - i * 0.5f) * s;
            add(MESH_CONE, glm::scale(glm::translate(glm::mat4(1.0f), p + glm::vec3(0, h + (0.4f + i * 1.1f) * s, 0)),
                                      glm::vec3(r * 2, 2.2f * s, r * 2)), lm);
        }
    } else {
        addSphere(p + glm::vec3(0, h + 1.1f * s, 0), glm::vec3(3.8f * s), lm);
        addSphere(p + glm::vec3(0.8f * s, h + 0.5f * s, 0.4f * s), glm::vec3(2.6f * s), lm);
        addSphere(p + glm::vec3(-0.7f * s, h + 0.7f * s, -0.4f * s), glm::vec3(2.8f * s), lm);
    }
}

// Street lamp on the footpath; the arm reaches over the road (towardRoad = unit vector)
void TownScene::addLamp(const glm::vec3& base, const glm::vec3& towardRoad) {
    Mat pole = mat({ 0.30f, 0.33f, 0.36f }, PAT_NONE, 0.5f, 32);
    addCyl(base + glm::vec3(0, 3.75f, 0), 0.11f, 7.5f, pole);
    glm::vec3 armC = base + towardRoad * 1.0f + glm::vec3(0, 7.4f, 0);
    addBox(armC, glm::abs(towardRoad) * 2.0f + glm::vec3(0.1f, 0.1f, 0.1f), pole);
    glm::vec3 head = base + towardRoad * 2.0f + glm::vec3(0, 7.3f, 0);
    addBox(head, glm::vec3(0.6f, 0.18f, 0.6f), pole);
    addSphere(head - glm::vec3(0, 0.15f, 0), glm::vec3(0.32f, 0.2f, 0.32f), glow({ 1, 0.85f, 0.5f }, { 1.3f, 0.85f, 0.35f }));
    lamps.push_back(head - glm::vec3(0, 0.3f, 0));
}

void TownScene::addBench(const glm::vec3& p, float yawDeg) {
    Mat wood = mat({ 0.45f, 0.28f, 0.15f }, PAT_NONE, 0.1f, 8);
    Mat iron = mat({ 0.12f, 0.13f, 0.14f }, PAT_NONE, 0.4f, 32);
    float a = glm::radians(yawDeg);
    glm::vec3 f(std::sin(a), 0, std::cos(a)), r(std::cos(a), 0, -std::sin(a));
    addBox(p + glm::vec3(0, 0.45f, 0), glm::vec3(1.8f, 0.07f, 0.5f), wood, yawDeg);
    addBox(p - f * 0.22f + glm::vec3(0, 0.75f, 0), glm::vec3(1.8f, 0.4f, 0.06f), wood, yawDeg);
    addBox(p + r * 0.8f + glm::vec3(0, 0.22f, 0), glm::vec3(0.06f, 0.45f, 0.5f), iron, yawDeg);
    addBox(p - r * 0.8f + glm::vec3(0, 0.22f, 0), glm::vec3(0.06f, 0.45f, 0.5f), iron, yawDeg);
}

void TownScene::addParkedCar(const glm::vec3& p, float yawDeg, const glm::vec3& color) {
    addBox(p + glm::vec3(0, 0.62f, 0), glm::vec3(1.8f, 0.62f, 4.4f), mat(color, PAT_NONE, 0.6f, 64), yawDeg);
    float a = glm::radians(yawDeg);
    glm::vec3 f(std::sin(a), 0, std::cos(a));
    addBox(p - f * 0.15f + glm::vec3(0, 1.2f, 0), glm::vec3(1.58f, 0.55f, 2.2f), mat({ 0.05f, 0.08f, 0.12f }, PAT_NONE, 0.9f, 96), yawDeg);
    addBox(p - f * 0.15f + glm::vec3(0, 1.5f, 0), glm::vec3(1.55f, 0.06f, 2.0f), mat(color, PAT_NONE, 0.6f, 64), yawDeg);
    addBox(p + glm::vec3(0, 0.33f, 0), glm::vec3(1.9f, 0.5f, 3.0f), mat({ 0.05f, 0.05f, 0.05f }, PAT_NONE, 0.1f, 8), yawDeg);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
enum BlockType { B_TOWERS, B_MOSQUE, B_PARK, B_SHOPS, B_SHOPS2, B_APART, B_HOUSES, B_SCHOOL, B_POLICE, B_BUS, B_FUEL };

// [bj][bi]: bi = 0..3 west -> east (x), bj = 0..3 north -> south (z)
static const BlockType LAYOUT[4][4] = {
    { B_APART,  B_SCHOOL, B_TOWERS, B_HOUSES },
    { B_HOUSES, B_TOWERS, B_MOSQUE, B_APART  },
    { B_SHOPS,  B_PARK,   B_SHOPS2, B_POLICE },
    { B_FUEL,   B_APART,  B_BUS,    B_HOUSES },
};

void TownScene::init() {
    prims.clear();
    lamps.clear();
    seed = 20260923u;
    buildRoads();
    for (int bj = 0; bj < 4; ++bj)
        for (int bi = 0; bi < 4; ++bi) buildBlock(bi, bj);
    buildOutskirts();
}

void TownScene::buildRoads() {
    Mat road = mat({ 0.2f, 0.2f, 0.2f }, PAT_ROAD, 0.2f, 16);
    const float y = 0.02f, h = 0.04f;
    for (int i = -HALF_N; i <= HALF_N; ++i) {
        float c = coord(i);
        float ext = (i == 0) ? HIGHWAY : EDGE + ROAD_HALF;   // the two main roads leave town
        addBox({ 0, y, c }, { 2 * ext, h, 2 * ROAD_HALF }, road);          // along X
        addBox({ c, y + 0.001f, 0 }, { 2 * ROAD_HALF, h, 2 * ext }, road); // along Z
    }

    // Street lamps: one per block side, alternating sides of the road; highway lamps every 40 m
    for (int line = -HALF_N; line <= HALF_N; ++line) {
        for (int seg = -HALF_N; seg < HALF_N; ++seg) {
            float mid = coord(seg) + GRID * 0.5f;
            const float off = ROAD_HALF + 0.5f;
            float side = ((line + seg) & 1) ? 1.0f : -1.0f;
            if (std::fabs(coord(line) + side * off) > EDGE) side = -side;          // keep inside the town
            addLamp({ mid, KERB_H, coord(line) + side * off }, { 0, 0, -side });   // road along X
            addLamp({ coord(line) + side * off, KERB_H, mid }, { -side, 0, 0 });   // road along Z
        }
    }
    for (float d = EDGE + 30.0f; d < 260.0f; d += 40.0f) {
        float off = ROAD_HALF + 0.8f;
        addLamp({ d, 0, off }, { 0, 0, -1 });
        addLamp({ -d, 0, -off }, { 0, 0, 1 });
        addLamp({ -off, 0, d }, { 1, 0, 0 });
        addLamp({ off, 0, -d }, { -1, 0, 0 });
    }
}

void TownScene::buildBlock(int bi, int bj) {
    glm::vec3 c(coord(bi - 2) + GRID * 0.5f, 0.0f, coord(bj - 2) + GRID * 0.5f);
    // Footpath slab over the whole block (lots are built on top of it)
    float blockW = GRID - 2.0f * ROAD_HALF;
    addBox(c + glm::vec3(0, KERB_H * 0.5f, 0), { blockW, KERB_H, blockW }, mat({ 0.4f, 0.4f, 0.4f }, PAT_PAVEMENT, 0.15f, 16));

    switch (LAYOUT[bj][bi]) {
        case B_TOWERS:  blockTowers(c); break;
        case B_MOSQUE:  blockMosque(c); break;
        case B_PARK:    blockPark(c); break;
        case B_SHOPS:   blockShops(c, 0); break;
        case B_SHOPS2:  blockShops(c, 1); break;
        case B_APART:   blockApartments(c); break;
        case B_HOUSES:  blockHouses(c); break;
        case B_SCHOOL:  blockSchool(c); break;
        case B_POLICE:  blockPolice(c); break;
        case B_BUS:     blockBusTerminal(c); break;
        case B_FUEL:    blockFuel(c); break;
    }
}

// Ground cover for a lot (slightly above the footpath)
static glm::vec3 lotTop(const glm::vec3& c) { return c + glm::vec3(0, KERB_H, 0); }

void TownScene::blockTowers(const glm::vec3& c) {
    glm::vec3 g = lotTop(c);
    addBox(g + glm::vec3(0, 0.02f, 0), { 2 * LOT_HALF, 0.04f, 2 * LOT_HALF }, mat({ 0.62f, 0.60f, 0.56f }, PAT_PLAZA));
    float hA = 34.0f + rnd() * 18.0f, hB = 22.0f + rnd() * 12.0f;
    Mat glassA = mat({ 0.55f, 0.58f, 0.62f }, PAT_GLASS, 0.6f, 96);
    addBox(g + glm::vec3(-4.5f, hA * 0.5f, -4.0f), { 11, hA, 11 }, glassA);
    addBox(g + glm::vec3(-4.5f, hA + 0.4f, -4.0f), { 11.6f, 0.8f, 11.6f }, mat({ 0.3f, 0.32f, 0.35f }));
    addBox(g + glm::vec3(-4.5f, hA + 2.0f, -4.0f), { 4, 2.4f, 4 }, mat({ 0.4f, 0.42f, 0.45f }, PAT_METAL));
    addCyl(g + glm::vec3(-3.0f, hA + 6.0f, -3.0f), 0.08f, 8.0f, mat({ 0.7f, 0.7f, 0.7f }));
    addSphere(g + glm::vec3(-3.0f, hA + 10.1f, -3.0f), glm::vec3(0.35f), glow({ 1, 0.1f, 0.1f }, { 2.0f, 0.1f, 0.1f }, false));
    addBox(g + glm::vec3(6.0f, hB * 0.5f, 5.0f), { 9, hB, 10 }, facade({ 0.78f, 0.74f, 0.66f }, { 2.0f, 3.5f }));
    addBox(g + glm::vec3(6.0f, hB + 0.4f, 5.0f), { 9.6f, 0.8f, 10.6f }, mat({ 0.5f, 0.48f, 0.45f }, PAT_CONCRETE));
    // entrance canopy facing the street (south)
    addBox(g + glm::vec3(6.0f, 3.4f, 10.6f), { 6, 0.25f, 2.4f }, mat({ 0.25f, 0.27f, 0.3f }, PAT_NONE, 0.6f, 64));
    addBox(g + glm::vec3(6.0f, 1.6f, 10.05f), { 3.0f, 3.0f, 0.15f }, glow({ 0.1f, 0.15f, 0.2f }, { 0.9f, 0.85f, 0.7f }));
    for (int i = 0; i < 4; ++i) addTree(g + glm::vec3(-10.0f + i * 3.0f, 0, 9.5f), 0.55f, false);
}

void TownScene::blockMosque(const glm::vec3& c) {
    glm::vec3 g = lotTop(c);
    Mat white = mat({ 0.92f, 0.90f, 0.84f }, PAT_CONCRETE, 0.2f, 16);
    Mat green = mat({ 0.10f, 0.45f, 0.30f }, PAT_NONE, 0.6f, 64);
    Mat gold = mat({ 0.85f, 0.65f, 0.2f }, PAT_NONE, 0.9f, 96);
    addBox(g + glm::vec3(0, 0.02f, 0), { 2 * LOT_HALF, 0.04f, 2 * LOT_HALF }, mat({ 0.78f, 0.74f, 0.66f }, PAT_PLAZA));

    // prayer hall with arched (window-grid) walls, faces the main road to the south
    glm::vec3 hall = g + glm::vec3(0, 0, -2.5f);
    addBox(hall + glm::vec3(0, 3.5f, 0), { 16, 7, 13 }, facade({ 0.93f, 0.91f, 0.86f }, { 2.6f, 5.0f }));
    addBox(hall + glm::vec3(0, 7.2f, 0), { 16.6f, 0.5f, 13.6f }, white);
    // central dome on a drum, small corner domes
    addCyl(hall + glm::vec3(0, 8.4f, 0), 4.2f, 2.0f, white);
    addSphere(hall + glm::vec3(0, 9.4f, 0), { 8.6f, 8.0f, 8.6f }, green);
    addCyl(hall + glm::vec3(0, 14.0f, 0), 0.08f, 1.6f, gold);
    addSphere(hall + glm::vec3(0, 14.9f, 0), glm::vec3(0.35f), gold);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            glm::vec3 p = hall + glm::vec3(sx * 6.8f, 7.4f, sz * 5.3f);
            addCyl(p + glm::vec3(0, 0.6f, 0), 0.9f, 1.2f, white);
            addSphere(p + glm::vec3(0, 1.3f, 0), glm::vec3(1.9f), green);
        }
    // two minarets at the front with balconies, green caps and lit finials
    for (int sx = -1; sx <= 1; sx += 2) {
        glm::vec3 m = g + glm::vec3(sx * 9.5f, 0, 8.5f);
        addCyl(m + glm::vec3(0, 9.0f, 0), 0.85f, 18.0f, white);
        addCyl(m + glm::vec3(0, 11.0f, 0), 1.25f, 0.35f, white);
        addCyl(m + glm::vec3(0, 16.0f, 0), 1.25f, 0.35f, white);
        addCyl(m + glm::vec3(0, 19.0f, 0), 0.6f, 2.0f, white);
        add(MESH_CONE, glm::scale(glm::translate(glm::mat4(1.0f), m + glm::vec3(0, 21.2f, 0)), glm::vec3(1.5f, 2.4f, 1.5f)), green);
        addSphere(m + glm::vec3(0, 22.7f, 0), glm::vec3(0.4f), glow({ 0.9f, 0.9f, 0.6f }, { 1.4f, 1.3f, 0.6f }));
    }
    // courtyard pool for ablution + boundary wall with a gate gap on the south side
    addBox(g + glm::vec3(0, 0.3f, 7.5f), { 7, 0.6f, 3 }, white);
    addBox(g + glm::vec3(0, 0.58f, 7.5f), { 6.4f, 0.04f, 2.4f }, mat({ 0.1f, 0.3f, 0.4f }, PAT_WATER, 1.0f, 128));
    Mat wall = mat({ 0.9f, 0.88f, 0.82f }, PAT_CONCRETE);
    addBox(g + glm::vec3(0, 0.8f, -LOT_HALF + 0.2f), { 2 * LOT_HALF, 1.6f, 0.35f }, wall);
    addBox(g + glm::vec3(-LOT_HALF + 0.2f, 0.8f, 0), { 0.35f, 1.6f, 2 * LOT_HALF }, wall);
    addBox(g + glm::vec3(LOT_HALF - 0.2f, 0.8f, 0), { 0.35f, 1.6f, 2 * LOT_HALF }, wall);
    addBox(g + glm::vec3(-7.5f, 0.8f, LOT_HALF - 0.2f), { 9, 1.6f, 0.35f }, wall);
    addBox(g + glm::vec3(7.5f, 0.8f, LOT_HALF - 0.2f), { 9, 1.6f, 0.35f }, wall);
    for (int i = 0; i < 2; ++i) addTree(g + glm::vec3(i ? 9.0f : -9.0f, 0, -9.0f), 0.6f, true);
}

void TownScene::blockPark(const glm::vec3& c) {
    glm::vec3 g = lotTop(c);
    addBox(g + glm::vec3(0, 0.02f, 0), { 2 * LOT_HALF, 0.04f, 2 * LOT_HALF }, mat({ 0.2f, 0.4f, 0.1f }, PAT_GRASS, 0.05f, 4));
    Mat path = mat({ 0.70f, 0.62f, 0.50f }, PAT_PLAZA);
    addBox(g + glm::vec3(0, 0.05f, 0), { 2 * LOT_HALF, 0.04f, 3.0f }, path);
    addBox(g + glm::vec3(0, 0.051f, 0), { 3.0f, 0.04f, 2 * LOT_HALF }, path);
    addCyl(g + glm::vec3(0, 0.05f, 0), 6.0f, 0.04f, path);

    // fountain basin (the jets are animated in renderFountain)
    Mat stone = mat({ 0.6f, 0.58f, 0.55f }, PAT_CONCRETE, 0.2f, 16);
    addCyl(g + glm::vec3(0, 0.35f, 0), 4.0f, 0.7f, stone);
    addCyl(g + glm::vec3(0, 0.72f, 0), 3.7f, 0.05f, mat({ 0.1f, 0.3f, 0.4f }, PAT_WATER, 1.0f, 128));
    addCyl(g + glm::vec3(0, 1.2f, 0), 0.5f, 1.4f, stone);
    addCyl(g + glm::vec3(0, 1.9f, 0), 1.6f, 0.2f, stone);

    // Shaheed Minar style memorial in the north-west corner: five columns, red sun disc behind
    glm::vec3 m = g + glm::vec3(-7.5f, 0, -7.5f);
    Mat marble = mat({ 0.95f, 0.95f, 0.93f }, PAT_NONE, 0.3f, 32);
    addBox(m + glm::vec3(0, 0.3f, 0), { 7.5f, 0.6f, 4.0f }, marble);
    const float colX[5] = { -2.6f, -1.3f, 0.0f, 1.3f, 2.6f };
    const float colH[5] = { 3.0f, 3.8f, 5.0f, 3.8f, 3.0f };
    for (int i = 0; i < 5; ++i) {
        float w = (i == 2) ? 0.9f : 0.6f;
        addBox(m + glm::vec3(colX[i] - w * 0.4f, 0.6f + colH[i] * 0.5f, 0), { 0.15f, colH[i], 0.15f }, marble);
        addBox(m + glm::vec3(colX[i] + w * 0.4f, 0.6f + colH[i] * 0.5f, 0), { 0.15f, colH[i], 0.15f }, marble);
        addBox(m + glm::vec3(colX[i], 0.6f + colH[i] - 0.1f, 0), { w, 0.15f, 0.15f }, marble);
    }
    // central column bows forward at the top
    addBox(m + glm::vec3(0, 6.0f, 0.45f), { 1.0f, 0.9f, 0.12f }, marble, 0.0f);
    glm::mat4 disc = glm::rotate(glm::translate(glm::mat4(1.0f), m + glm::vec3(0, 3.2f, -0.9f)), glm::radians(90.0f), glm::vec3(1, 0, 0));
    add(MESH_CYLINDER, glm::scale(disc, glm::vec3(4.6f, 0.1f, 4.6f)), mat({ 0.8f, 0.05f, 0.05f }, PAT_NONE, 0.3f, 32));

    // trees, benches, park lamps
    const glm::vec2 treePos[] = { { 8, -8 }, { 9.5f, -3 }, { 9, 4 }, { 8, 9 }, { -9, 8.5f }, { -4, 9.5f }, { 4, -9.5f }, { -9.5f, 3.5f }, { 3.5f, 9.5f } };
    for (const auto& t : treePos) addTree(g + glm::vec3(t.x, 0, t.y), 0.75f + 0.3f * rnd(), rnd() < 0.3f);
    addBench(g + glm::vec3(5.0f, 0, 2.6f), 180);
    addBench(g + glm::vec3(-5.0f, 0, 2.6f), 180);
    addBench(g + glm::vec3(5.0f, 0, -2.6f), 0);
    addBench(g + glm::vec3(2.6f, 0, 6.0f), 270);
    addBench(g + glm::vec3(-2.6f, 0, -6.0f), 90);
    for (int i = 0; i < 4; ++i) {
        float a = glm::radians(45.0f + 90.0f * i);
        glm::vec3 p = g + glm::vec3(std::cos(a) * 7.0f, 0, std::sin(a) * 7.0f);
        addCyl(p + glm::vec3(0, 1.6f, 0), 0.07f, 3.2f, mat({ 0.1f, 0.1f, 0.1f }, PAT_NONE, 0.5f, 32));
        addSphere(p + glm::vec3(0, 3.35f, 0), glm::vec3(0.45f), glow({ 1, 0.95f, 0.8f }, { 1.2f, 1.0f, 0.7f }));
    }
}

// Ring of 2-3 storey shop houses facing all four streets
void TownScene::blockShops(const glm::vec3& c, int variant) {
    glm::vec3 g = lotTop(c);
    addBox(g + glm::vec3(0, 0.02f, 0), { 2 * LOT_HALF, 0.04f, 2 * LOT_HALF }, mat({ 0.35f, 0.33f, 0.3f }, PAT_CONCRETE));
    static const glm::vec3 paints[] = {
        { 0.92f, 0.80f, 0.55f }, { 0.75f, 0.85f, 0.80f }, { 0.95f, 0.70f, 0.60f }, { 0.80f, 0.78f, 0.92f },
        { 0.95f, 0.92f, 0.75f }, { 0.70f, 0.80f, 0.90f }, { 0.90f, 0.60f, 0.45f } };
    static const glm::vec3 signs[] = {
        { 1.0f, 0.2f, 0.2f }, { 0.2f, 0.6f, 1.0f }, { 0.1f, 0.9f, 0.4f }, { 1.0f, 0.75f, 0.1f }, { 0.9f, 0.3f, 0.9f } };

    struct Shop { glm::vec3 pos; glm::vec3 out; float width; };
    std::vector<Shop> shops;
    for (int side = 0; side < 4; ++side) {
        glm::vec3 out = side == 0 ? glm::vec3(0, 0, -1) : side == 1 ? glm::vec3(0, 0, 1) : side == 2 ? glm::vec3(-1, 0, 0) : glm::vec3(1, 0, 0);
        glm::vec3 along(std::fabs(out.z), 0, std::fabs(out.x));
        if (side < 2) {
            for (int k = -1; k <= 1; ++k) shops.push_back({ out * (LOT_HALF - 3.5f) + along * (k * 8.0f), out, 7.6f });
        } else {
            shops.push_back({ out * (LOT_HALF - 3.5f), out, 9.6f });
        }
    }
    int n = 0;
    for (const Shop& s : shops) {
        int floors = 2 + static_cast<int>(rnd() * 2.0f) + variant;
        float h = floors * 3.3f;
        bool alongX = std::fabs(s.out.z) > 0.5f;
        glm::vec3 size = alongX ? glm::vec3(s.width, h, 7.0f) : glm::vec3(7.0f, h, s.width);
        glm::vec3 base = g + s.pos;
        glm::vec3 paint = paints[(n + variant * 3) % 7];
        addBox(base + glm::vec3(0, h * 0.5f, 0), size, facade(paint, { 2.4f, 3.3f }));
        addBox(base + glm::vec3(0, h + 0.45f, 0), { size.x + 0.3f, 0.9f, size.z + 0.3f }, mat(paint * 0.8f, PAT_CONCRETE));
        // water tank on the roof (a Bangladeshi rooftop classic)
        addCyl(base + glm::vec3(1.5f, h + 1.4f, -0.5f), 0.7f, 1.4f, mat({ 0.06f, 0.06f, 0.07f }, PAT_NONE, 0.6f, 48));

        // shop front: lit glass, awning, sign board (all on the street face)
        glm::vec3 front = base + s.out * 3.55f;
        glm::vec3 along(std::fabs(s.out.z), 0, std::fabs(s.out.x));
        float w = s.width - 1.0f;
        glm::vec3 sgnC = signs[(n * 3 + variant) % 5];
        addBox(front + glm::vec3(0, 1.45f, 0), along * w + glm::abs(s.out) * 0.12f + glm::vec3(0, 2.5f, 0),
               glow({ 0.15f, 0.12f, 0.08f }, { 0.42f, 0.31f, 0.19f }));
        addBox(front + s.out * 0.7f + glm::vec3(0, 3.0f, 0), along * (w + 0.4f) + glm::abs(s.out) * 1.5f + glm::vec3(0, 0.12f, 0),
               mat(n % 2 ? glm::vec3(0.75f, 0.15f, 0.12f) : glm::vec3(0.12f, 0.35f, 0.6f), PAT_NONE, 0.3f, 16));
        Mat sign = glow(sgnC * 0.6f, sgnC * 0.75f);
        addBox(front + s.out * 0.1f + glm::vec3(0, 3.75f, 0), along * (w * 0.8f) + glm::abs(s.out) * 0.15f + glm::vec3(0, 0.75f, 0), sign);
        ++n;
    }
}

void TownScene::blockApartments(const glm::vec3& c) {
    glm::vec3 g = lotTop(c);
    addBox(g + glm::vec3(0, 0.02f, 0), { 2 * LOT_HALF, 0.04f, 2 * LOT_HALF }, mat({ 0.4f, 0.38f, 0.35f }, PAT_CONCRETE));
    static const glm::vec3 paints[] = { { 0.90f, 0.86f, 0.76f }, { 0.82f, 0.72f, 0.62f }, { 0.78f, 0.82f, 0.85f }, { 0.92f, 0.80f, 0.70f } };
    for (int k = 0; k < 2; ++k) {
        float floors = 6.0f + std::floor(rnd() * 3.0f);
        float h = floors * 3.0f;
        glm::vec3 base = g + glm::vec3(k ? 6.0f : -6.0f, 0, -1.0f);
        glm::vec3 paint = paints[static_cast<int>(rnd() * 4) % 4];
        addBox(base + glm::vec3(0, h * 0.5f, 0), { 10, h, 18 }, facade(paint, { 2.2f, 3.0f }));
        addBox(base + glm::vec3(0, h + 0.5f, 0), { 10.3f, 1.0f, 18.3f }, mat(paint * 0.75f, PAT_CONCRETE));
        // balconies on the street-facing (south) side
        Mat slab = mat(paint * 0.85f, PAT_CONCRETE);
        Mat rail = mat({ 0.2f, 0.22f, 0.25f }, PAT_NONE, 0.5f, 32);
        for (int f = 1; f < static_cast<int>(floors); ++f)
            for (int b = -1; b <= 1; b += 2) {
                glm::vec3 p = base + glm::vec3(b * 2.6f, f * 3.0f, 9.6f);
                addBox(p, { 3.0f, 0.15f, 1.2f }, slab);
                addBox(p + glm::vec3(0, 0.55f, 0.55f), { 3.0f, 1.0f, 0.08f }, rail);
            }
        // rooftop water tanks + stair room
        addCyl(base + glm::vec3(-2.5f, h + 1.8f, -5.0f), 0.8f, 1.6f, mat({ 0.06f, 0.06f, 0.07f }, PAT_NONE, 0.6f, 48));
        addCyl(base + glm::vec3(-0.6f, h + 1.8f, -5.0f), 0.8f, 1.6f, mat({ 0.06f, 0.06f, 0.07f }, PAT_NONE, 0.6f, 48));
        addBox(base + glm::vec3(2.5f, h + 2.0f, -3.0f), { 3.5f, 3.0f, 4.0f }, mat(paint * 0.9f, PAT_CONCRETE));
    }
    // boundary wall along the south street with a gate gap, a tree each side
    Mat wall = mat({ 0.6f, 0.58f, 0.55f }, PAT_CONCRETE);
    addBox(g + glm::vec3(-7.0f, 0.75f, LOT_HALF - 0.2f), { 10, 1.5f, 0.3f }, wall);
    addBox(g + glm::vec3(7.0f, 0.75f, LOT_HALF - 0.2f), { 10, 1.5f, 0.3f }, wall);
    addTree(g + glm::vec3(-10.0f, 0, 10.0f), 0.6f, false);
    addTree(g + glm::vec3(10.0f, 0, 10.0f), 0.6f, false);
}

void TownScene::blockHouses(const glm::vec3& c) {
    glm::vec3 g = lotTop(c);
    addBox(g + glm::vec3(0, 0.02f, 0), { 2 * LOT_HALF, 0.04f, 2 * LOT_HALF }, mat({ 0.2f, 0.4f, 0.1f }, PAT_GRASS, 0.05f, 4));
    static const glm::vec3 walls[] = { { 0.93f, 0.88f, 0.75f }, { 0.85f, 0.65f, 0.50f }, { 0.75f, 0.82f, 0.70f }, { 0.95f, 0.95f, 0.90f } };
    static const glm::vec3 roofs[] = { { 0.55f, 0.18f, 0.10f }, { 0.25f, 0.28f, 0.32f }, { 0.45f, 0.25f, 0.15f } };
    for (int ix = -1; ix <= 1; ix += 2)
        for (int iz = -1; iz <= 1; iz += 2) {
            glm::vec3 base = g + glm::vec3(ix * 6.0f, 0, iz * 6.0f);
            bool twoStorey = rnd() < 0.4f;
            float h = twoStorey ? 6.2f : 3.2f;
            float yaw = (iz > 0) ? 0.0f : 180.0f;
            glm::vec3 wallC = walls[static_cast<int>(rnd() * 4) % 4];
            addBox(base + glm::vec3(0, h * 0.5f, 0), { 7.5f, h, 6.5f }, facade(wallC, { 2.5f, 3.0f }));
            addRoof(base + glm::vec3(0, h + 1.1f, 0), { 8.3f, 2.2f, 7.3f }, mat(roofs[static_cast<int>(rnd() * 3) % 3], PAT_ROOFTILE, 0.2f, 16), 90.0f);
            glm::vec3 doorSide(0, 0, iz * 3.27f);
            addBox(base + doorSide + glm::vec3(-1.5f, 1.05f, 0), { 1.0f, 2.1f, 0.06f }, mat({ 0.35f, 0.2f, 0.1f }, PAT_NONE, 0.2f, 16), yaw);
            addBox(base + glm::vec3(0, 0.4f, iz * 5.6f) + glm::vec3(ix * 1.5f, 0, 0), { 4, 0.8f, 0.2f }, mat({ 0.7f, 0.68f, 0.62f }, PAT_BRICK));
            addTree(base + glm::vec3(ix * 3.5f, 0, iz * 4.7f), 0.5f, rnd() < 0.5f);
        }
}

void TownScene::blockSchool(const glm::vec3& c) {
    glm::vec3 g = lotTop(c);
    addBox(g + glm::vec3(0, 0.02f, 0), { 2 * LOT_HALF, 0.04f, 2 * LOT_HALF }, mat({ 0.2f, 0.4f, 0.1f }, PAT_GRASS, 0.05f, 4));
    // L-shaped red-brick school, three storeys
    Mat brick = mat({ 0.62f, 0.30f, 0.22f }, PAT_BRICK, 0.1f, 8);
    addBox(g + glm::vec3(-7.0f, 5.0f, 0), { 8, 10, 22 }, brick);
    addBox(g + glm::vec3(2.0f, 5.0f, -8.0f), { 12, 10, 6 }, brick);
    Mat trim = mat({ 0.92f, 0.9f, 0.85f }, PAT_CONCRETE);
    for (int f = 1; f <= 3; ++f) {
        addBox(g + glm::vec3(-2.9f, f * 3.2f - 0.2f, 0), { 0.3f, 0.25f, 22.2f }, trim);       // floor bands
        addBox(g + glm::vec3(2.0f, f * 3.2f - 0.2f, -4.9f), { 12.2f, 0.25f, 0.3f }, trim);
        for (int w = -3; w <= 3; ++w)                                                        // windows
            addBox(g + glm::vec3(-2.95f, f * 3.2f - 1.5f, w * 3.0f), { 0.1f, 1.4f, 1.6f }, facade({ 0.1f, 0.12f, 0.15f }, { 100.0f, 100.0f }));
    }
    addBox(g + glm::vec3(-7.0f, 10.25f, 0), { 8.4f, 0.5f, 22.4f }, trim);
    addBox(g + glm::vec3(2.0f, 10.25f, -8.0f), { 12.4f, 0.5f, 6.4f }, trim);
    // playground: two goal posts
    Mat post = mat({ 0.95f, 0.95f, 0.95f }, PAT_NONE, 0.3f, 32);
    for (int s = -1; s <= 1; s += 2) {
        glm::vec3 p = g + glm::vec3(4.5f + s * 6.0f, 0, 4.0f);
        addCyl(p + glm::vec3(0, 1.1f, -1.6f), 0.06f, 2.2f, post);
        addCyl(p + glm::vec3(0, 1.1f, 1.6f), 0.06f, 2.2f, post);
        addBox(p + glm::vec3(0, 2.2f, 0), { 0.12f, 0.12f, 3.3f }, post);
    }
    // flag pole with the national flag (green field, red disc)
    glm::vec3 fp = g + glm::vec3(4.0f, 0, 10.5f);
    addCyl(fp + glm::vec3(0, 4.0f, 0), 0.06f, 8.0f, post);
    addBox(fp + glm::vec3(1.0f, 7.2f, 0), { 2.0f, 1.2f, 0.04f }, mat({ 0.0f, 0.42f, 0.30f }, PAT_NONE, 0.2f, 16));
    glm::mat4 disc = glm::rotate(glm::translate(glm::mat4(1.0f), fp + glm::vec3(0.9f, 7.2f, 0)), glm::radians(90.0f), glm::vec3(1, 0, 0));
    add(MESH_CYLINDER, glm::scale(disc, glm::vec3(0.8f, 0.07f, 0.8f)), mat({ 0.85f, 0.1f, 0.15f }, PAT_NONE, 0.2f, 16));
    addTree(g + glm::vec3(10.0f, 0, -9.0f), 0.8f, false);
    addTree(g + glm::vec3(10.0f, 0, 10.0f), 0.7f, false);
}

// NightWatch HQ: walled security compound (the original project's compound, scaled to a block)
void TownScene::blockPolice(const glm::vec3& c) {
    glm::vec3 g = lotTop(c);
    addBox(g + glm::vec3(0, 0.02f, 0), { 2 * LOT_HALF, 0.04f, 2 * LOT_HALF }, mat({ 0.35f, 0.35f, 0.36f }, PAT_CONCRETE));
    Mat brick = mat({ 0.58f, 0.30f, 0.22f }, PAT_BRICK, 0.1f, 8);
    const float wh = 3.0f, L = LOT_HALF - 0.3f;
    addBox(g + glm::vec3(0, wh * 0.5f, -L), { 2 * L, wh, 0.6f }, brick);
    addBox(g + glm::vec3(-L, wh * 0.5f, 0), { 0.6f, wh, 2 * L }, brick);
    addBox(g + glm::vec3(L, wh * 0.5f, 0), { 0.6f, wh, 2 * L }, brick);
    addBox(g + glm::vec3(-7.5f, wh * 0.5f, L), { 9, wh, 0.6f }, brick);   // gate gap in the south wall
    addBox(g + glm::vec3(7.5f, wh * 0.5f, L), { 9, wh, 0.6f }, brick);
    Mat hazard = mat({ 0.9f, 0.25f, 0.15f }, PAT_NONE, 0.4f, 32);
    for (int s = -1; s <= 1; s += 2) {
        addBox(g + glm::vec3(s * 3.2f, 2.0f, L), { 1.0f, 4.0f, 1.0f }, mat({ 0.55f, 0.55f, 0.55f }, PAT_CONCRETE));
        addBox(g + glm::vec3(s * 3.2f, 4.15f, L), { 1.2f, 0.3f, 1.2f }, hazard);
    }
    // HQ building with a glowing blue sign
    addBox(g + glm::vec3(-3.0f, 4.0f, -5.5f), { 14, 8, 9 }, facade({ 0.78f, 0.80f, 0.84f }, { 2.4f, 3.6f }));
    addBox(g + glm::vec3(-3.0f, 8.3f, -5.5f), { 14.4f, 0.6f, 9.4f }, mat({ 0.25f, 0.27f, 0.3f }));
    addBox(g + glm::vec3(-3.0f, 6.8f, -0.9f), { 7.0f, 1.0f, 0.2f }, glow({ 0.1f, 0.2f, 0.6f }, { 0.3f, 0.6f, 1.6f }));
    // watch tower
    glm::vec3 t = g + glm::vec3(8.5f, 0, -8.5f);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            addBox(t + glm::vec3(sx * 1.1f, 4.0f, sz * 1.1f), { 0.25f, 8.0f, 0.25f }, mat({ 0.3f, 0.3f, 0.32f }, PAT_METAL));
    addBox(t + glm::vec3(0, 8.8f, 0), { 3.2f, 1.6f, 3.2f }, mat({ 0.40f, 0.35f, 0.30f }, PAT_METAL));
    addBox(t + glm::vec3(0, 9.8f, 0), { 3.8f, 0.25f, 3.8f }, mat({ 0.28f, 0.3f, 0.32f }));
    // radio mast with a red beacon
    addCyl(g + glm::vec3(-9.0f, 9.0f, -9.0f), 0.12f, 18.0f, mat({ 0.7f, 0.7f, 0.72f }, PAT_NONE, 0.5f, 32));
    addSphere(g + glm::vec3(-9.0f, 18.2f, -9.0f), glm::vec3(0.4f), glow({ 1, 0.1f, 0.1f }, { 2.0f, 0.1f, 0.1f }, false));
    // shipping containers + parked police cars
    addBox(g + glm::vec3(7.0f, 1.3f, 2.0f), { 2.44f, 2.6f, 6.1f }, mat({ 0.12f, 0.33f, 0.55f }, PAT_METAL, 0.35f, 24));
    addBox(g + glm::vec3(7.0f, 3.9f, 2.0f), { 2.44f, 2.6f, 6.1f }, mat({ 0.72f, 0.26f, 0.12f }, PAT_METAL, 0.35f, 24));
    addParkedCar(g + glm::vec3(-8.0f, 0, 5.0f), 0, { 0.92f, 0.92f, 0.92f });
    addParkedCar(g + glm::vec3(-5.0f, 0, 5.0f), 0, { 0.15f, 0.25f, 0.55f });
}

void TownScene::blockBusTerminal(const glm::vec3& c) {
    glm::vec3 g = lotTop(c);
    addBox(g + glm::vec3(0, 0.02f, 0), { 2 * LOT_HALF, 0.04f, 2 * LOT_HALF }, mat({ 0.2f, 0.2f, 0.2f }, PAT_NONE, 0.2f, 16));
    // parking bay lines
    Mat line = mat({ 0.85f, 0.85f, 0.8f });
    for (int i = -3; i <= 3; ++i) addBox(g + glm::vec3(i * 3.2f, 0.05f, -6.0f), { 0.12f, 0.02f, 5.0f }, line);
    for (int i = -3; i < 3; ++i) addParkedCar(g + glm::vec3(i * 3.2f + 1.6f, 0.04f, -6.0f), (i & 1) ? 0.0f : 180.0f,
                                              glm::vec3(0.2f + 0.6f * rnd(), 0.2f + 0.5f * rnd(), 0.2f + 0.6f * rnd()));
    // two parked buses
    for (int k = 0; k < 2; ++k) {
        glm::vec3 b = g + glm::vec3(-6.0f + k * 4.0f, 0, 5.0f);
        glm::vec3 col = k ? glm::vec3(0.1f, 0.45f, 0.25f) : glm::vec3(0.75f, 0.1f, 0.08f);
        addBox(b + glm::vec3(0, 1.65f, 0), { 2.5f, 2.6f, 11.0f }, mat(col, PAT_NONE, 0.5f, 48));
        addBox(b + glm::vec3(0, 2.15f, 0.3f), { 2.54f, 0.9f, 9.0f }, mat({ 0.05f, 0.08f, 0.1f }, PAT_NONE, 0.9f, 96));
    }
    // passenger shelter with a long canopy
    Mat steel = mat({ 0.55f, 0.58f, 0.6f }, PAT_NONE, 0.5f, 48);
    for (int i = 0; i < 4; ++i) addCyl(g + glm::vec3(4.0f + i * 2.5f, 1.5f, 6.0f), 0.08f, 3.0f, steel);
    addBox(g + glm::vec3(7.75f, 3.05f, 6.0f), { 9.0f, 0.15f, 3.5f }, mat({ 0.1f, 0.4f, 0.3f }, PAT_METAL));
    addBox(g + glm::vec3(7.75f, 0.45f, 6.6f), { 8.0f, 0.1f, 0.6f }, steel);
    addBox(g + glm::vec3(7.75f, 3.5f, 4.3f), { 6.0f, 0.7f, 0.12f }, glow({ 0.9f, 0.6f, 0.1f }, { 1.4f, 0.9f, 0.2f }));
}

void TownScene::blockFuel(const glm::vec3& c) {
    glm::vec3 g = lotTop(c);
    addBox(g + glm::vec3(0, 0.02f, 0), { 2 * LOT_HALF, 0.04f, 2 * LOT_HALF }, mat({ 0.45f, 0.45f, 0.45f }, PAT_CONCRETE));
    Mat white = mat({ 0.95f, 0.95f, 0.95f }, PAT_NONE, 0.4f, 32);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            addCyl(g + glm::vec3(2.0f + sx * 4.0f, 2.5f, 3.0f + sz * 3.0f), 0.25f, 5.0f, white);
    addBox(g + glm::vec3(2.0f, 5.3f, 3.0f), { 12.0f, 0.6f, 9.0f }, white);
    addBox(g + glm::vec3(2.0f, 5.0f, 3.0f), { 12.1f, 0.25f, 9.1f }, glow({ 0.8f, 0.1f, 0.1f }, { 1.4f, 0.15f, 0.1f }));
    addBox(g + glm::vec3(2.0f, 4.9f, 3.0f), { 11.0f, 0.05f, 8.0f }, glow({ 0.9f, 0.9f, 0.9f }, { 1.6f, 1.6f, 1.5f }));
    for (int k = -1; k <= 1; k += 2) {
        glm::vec3 p = g + glm::vec3(2.0f + k * 2.0f, 0, 3.0f);
        addBox(p + glm::vec3(0, 0.15f, 0), { 1.2f, 0.3f, 4.0f }, mat({ 0.6f, 0.6f, 0.6f }, PAT_CONCRETE));
        addBox(p + glm::vec3(0, 1.0f, 0), { 0.6f, 1.5f, 0.9f }, mat({ 0.85f, 0.15f, 0.12f }, PAT_NONE, 0.5f, 48));
        addBox(p + glm::vec3(0, 1.4f, 0.46f), { 0.4f, 0.3f, 0.02f }, glow({ 0.1f, 0.1f, 0.1f }, { 0.3f, 1.0f, 0.5f }));
    }
    addBox(g + glm::vec3(-7.0f, 2.0f, -6.0f), { 8, 4, 8 }, facade({ 0.92f, 0.92f, 0.9f }, { 2.6f, 4.0f }));
    addBox(g + glm::vec3(-7.0f, 4.2f, -6.0f), { 8.4f, 0.4f, 8.4f }, mat({ 0.8f, 0.1f, 0.1f }));
    addCyl(g + glm::vec3(-10.0f, 3.5f, 10.0f), 0.15f, 7.0f, white);
    addBox(g + glm::vec3(-10.0f, 7.0f, 10.0f), { 2.2f, 1.6f, 0.3f }, glow({ 0.9f, 0.1f, 0.1f }, { 1.5f, 0.3f, 0.2f }));
    addParkedCar(g + glm::vec3(0.0f, 0.04f, 3.0f), 0.0f, { 0.12f, 0.22f, 0.48f });
}

void TownScene::buildOutskirts() {
    // trees in a ring outside the town, kept off the highways
    int placed = 0, attempts = 0;
    while (placed < 110 && attempts++ < 3000) {
        float ang = rnd() * 6.2831853f;
        float rad = 100.0f + rnd() * 130.0f;
        glm::vec3 p(std::cos(ang) * rad, 0, std::sin(ang) * rad);
        if (std::fabs(p.x) < 12.0f || std::fabs(p.z) < 12.0f) continue;
        addTree(p, 0.8f + rnd() * 0.8f, rnd() < 0.3f);
        ++placed;
    }
    // village houses with tin roofs
    for (int i = 0; i < 14; ++i) {
        float ang = rnd() * 6.2831853f;
        float rad = 110.0f + rnd() * 70.0f;
        glm::vec3 p(std::cos(ang) * rad, 0, std::sin(ang) * rad);
        if (std::fabs(p.x) < 15.0f || std::fabs(p.z) < 15.0f) continue;
        float yaw = rnd() * 90.0f;
        addBox(p + glm::vec3(0, 1.4f, 0), { 6, 2.8f, 4.5f }, mat({ 0.55f, 0.45f, 0.32f }, PAT_BRICK, 0.1f, 8), yaw);
        addRoof(p + glm::vec3(0, 3.5f, 0), { 6.8f, 1.4f, 5.2f }, mat({ 0.45f, 0.47f, 0.5f }, PAT_METAL, 0.5f, 32), yaw + 90.0f);
    }
    // distant hills that fade into the fog
    for (int i = 0; i < 12; ++i) {
        float ang = i * 0.5236f + 0.2f;
        float rad = 290.0f + 30.0f * std::sin(i * 2.1f);
        addSphere(glm::vec3(std::cos(ang) * rad, -10.0f, std::sin(ang) * rad),
                  glm::vec3(150.0f, 44.0f + 16.0f * std::sin(i * 1.7f), 120.0f), mat({ 0.20f, 0.30f, 0.16f }, PAT_FOLIAGE, 0.02f, 4));
    }
}

// ---------------------------------------------------------------------------
// Frame rendering
// ---------------------------------------------------------------------------
void TownScene::render(const Shader& shader, const GeometryManager& geo, const CCTVKinematicChain& cctv,
                       const TrafficSystem& traffic, float time, bool shadowPass) const {
    Painter p(shader, geo, lampGlow, shadowPass);
    if (!shadowPass) {
        shader.setInt("uShadingModel", static_cast<int>(currentShadingModel));
        shader.setVec2("uFacadeCell", glm::vec2(2.6f, 3.4f));
    }

    // ground plane (grass outside, asphalt under the town)
    p.mat(Mat({ 0.2f, 0.2f, 0.2f }, glm::vec3(0.1f), 8.0f, PAT_GROUND));
    shader.setMat4("uModel", glm::mat4(1.0f));
    geo.drawGround();

    for (const Prim& pr : prims) {
        p.mat(pr.mat);
        p.draw(pr.mesh, pr.model);
    }
    renderFountain(p, time);
    renderSignals(p, traffic);
    renderCCTV(p, cctv, time);
    renderVehicles(p, traffic, time);
    renderPedestrians(p, traffic);
    if (drawBezierGuide && !shadowPass) renderRouteGuide(p, traffic);
}
