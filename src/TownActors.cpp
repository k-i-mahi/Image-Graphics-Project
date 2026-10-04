// Moving parts of the town: traffic signals, vehicles, pedestrians, CCTV, fountain.
// Every model is built from unit primitives with hierarchical transforms
// (parent matrix * local translate/rotate/scale).
#include "Town.h"
#include "Traffic.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace Town;

static const glm::vec3 X_AXIS(1, 0, 0), Y_AXIS(0, 1, 0), Z_AXIS(0, 0, 1);

static Mat plain(glm::vec3 d, float spec = 0.3f, float shin = 32.0f) { return Mat(d, glm::vec3(spec), shin, PAT_NONE); }
static Mat light(glm::vec3 d, glm::vec3 e, bool lamp) {
    Mat m(d, glm::vec3(0.6f), 64.0f, PAT_NONE);
    m.emissive = e;
    m.lamp = lamp;
    return m;
}

// ---------------------------------------------------------------------------
// Traffic signals: one head per approach, facing the oncoming traffic
// ---------------------------------------------------------------------------
void TownScene::renderSignals(Painter& p, const TrafficSystem& traffic) const {
    const Mat pole = plain({ 0.18f, 0.19f, 0.2f }, 0.4f);
    const Mat housing = plain({ 0.05f, 0.05f, 0.05f }, 0.3f);
    const Mat lit[3] = { light({ 0.1f, 1.0f, 0.4f }, { 0.2f, 2.6f, 0.8f }, false),
                         light({ 1.0f, 0.7f, 0.05f }, { 2.6f, 1.6f, 0.1f }, false),
                         light({ 1.0f, 0.1f, 0.05f }, { 2.8f, 0.15f, 0.08f }, false) };
    const glm::vec3 dirs[4] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (int i = -HALF_N; i <= HALF_N; ++i)
        for (int j = -HALF_N; j <= HALF_N; ++j) {
            glm::vec3 J(coord(i), 0, coord(j));
            if (glm::length(J - cullCenter) > 160.0f) continue;
            for (const glm::vec3& d : dirs) {
                // does a road arrive from that side?
                int fi = i - static_cast<int>(d.x), fj = j - static_cast<int>(d.z);
                bool alongX = std::fabs(d.x) > 0.5f;
                bool exists = (std::abs(fi) <= HALF_N && std::abs(fj) <= HALF_N) || (alongX ? j == 0 : i == 0);
                if (!exists) continue;
                glm::vec3 base = J - d * (ROAD_HALF + 0.5f) + leftOf(d) * (ROAD_HALF + 0.5f) + glm::vec3(0, KERB_H, 0);
                p.mat(pole);
                p.cyl(glm::mat4(1.0f), base + glm::vec3(0, 1.9f, 0), 0.07f, 3.8f);
                glm::mat4 head = glm::translate(glm::mat4(1.0f), base + glm::vec3(0, 3.35f, 0));
                head = glm::rotate(head, std::atan2(-d.x, -d.z), Y_AXIS);   // local +z faces the drivers
                p.mat(housing);
                p.box(head, glm::vec3(0, 0, 0), { 0.36f, 1.05f, 0.26f });
                int state = traffic.signal(i, j, alongX ? AXIS_X : AXIS_Z);
                const float bulbY[3] = { -0.33f, 0.0f, 0.33f };              // green, yellow, red
                p.mat(lit[state]);
                p.sphere(head, { 0, bulbY[state], 0.13f }, glm::vec3(0.22f));
            }
        }
}

// ---------------------------------------------------------------------------
// Vehicles
// ---------------------------------------------------------------------------
static void wheel(Painter& p, const glm::mat4& root, float x, float z, float r, float w, float spinDeg, bool spokes = false) {
    glm::mat4 m = glm::translate(root, glm::vec3(x, r, z));
    m = glm::rotate(m, glm::radians(spinDeg), X_AXIS);      // rolling about the axle
    m = glm::rotate(m, glm::radians(90.0f), Z_AXIS);        // cylinder axis -> axle (x)
    p.mat(plain({ 0.06f, 0.06f, 0.06f }, 0.15f, 8));
    p.draw(MESH_CYLINDER, glm::scale(m, glm::vec3(2 * r, w, 2 * r)));
    p.mat(plain({ 0.62f, 0.64f, 0.66f }, 0.8f, 64));
    if (spokes) {
        p.draw(MESH_CUBE, glm::scale(m, glm::vec3(0.04f, w * 0.6f, 1.9f * r)));
        p.draw(MESH_CUBE, glm::scale(m, glm::vec3(1.9f * r, w * 0.6f, 0.04f)));
    } else {
        p.draw(MESH_CYLINDER, glm::scale(m, glm::vec3(1.15f * r, w * 1.04f, 1.15f * r)));
        p.draw(MESH_CUBE, glm::scale(m, glm::vec3(0.12f, w * 1.08f, 1.0f * r)));   // makes the spin visible
    }
}

static void headTail(Painter& p, const glm::mat4& root, float halfW, float halfL, float y, bool braking) {
    p.mat(light({ 1.0f, 0.95f, 0.85f }, { 1.6f, 1.5f, 1.2f }, true));
    p.box(root, { -halfW + 0.3f, y, halfL + 0.02f }, { 0.36f, 0.16f, 0.06f });
    p.box(root, { halfW - 0.3f, y, halfL + 0.02f }, { 0.36f, 0.16f, 0.06f });
    p.mat(light({ 0.8f, 0.05f, 0.05f }, braking ? glm::vec3(2.4f, 0.1f, 0.05f) : glm::vec3(0.7f, 0.02f, 0.02f), false));
    p.box(root, { -halfW + 0.25f, y, -halfL - 0.02f }, { 0.32f, 0.16f, 0.06f });
    p.box(root, { halfW - 0.25f, y, -halfL - 0.02f }, { 0.32f, 0.16f, 0.06f });
}

struct Look { glm::vec3 shirt, pants, skin, hair; int style; float h; };

// Hierarchical human: pelvis -> torso -> head, shoulder -> elbow, hip -> knee.
//   legF[2]  : hip swing forward (rad)   knee[2] : knee bend (rad)   arm[2] : shoulder swing forward
static void drawPerson(Painter& p, const glm::mat4& root, const Look& L, float hipY,
                       const float legF[2], const float knee[2], const float arm[2], float elbow) {
    const float h = L.h;
    glm::mat4 pelvis = glm::translate(root, glm::vec3(0, hipY, 0));
    Mat shirt = plain(L.shirt, 0.1f, 8), pants = plain(L.pants, 0.1f, 8), skin = plain(L.skin, 0.2f, 16);
    Mat shoes = plain({ 0.05f, 0.04f, 0.04f }, 0.3f, 16);

    // legs
    for (int s = 0; s < 2; ++s) {
        float sx = s == 0 ? -0.1f * h : 0.1f * h;
        glm::mat4 hip = glm::rotate(glm::translate(pelvis, glm::vec3(sx, 0, 0)), -legF[s], X_AXIS);
        p.mat(L.style == 0 ? pants : skin);
        if (L.style == 0) p.box(hip, { 0, -0.22f * h, 0 }, { 0.15f * h, 0.46f * h, 0.16f * h });
        glm::mat4 kn = glm::rotate(glm::translate(hip, glm::vec3(0, -0.44f * h, 0)), knee[s], X_AXIS);
        p.mat(L.style == 0 ? pants : skin);
        p.box(kn, { 0, -0.21f * h, 0 }, { 0.12f * h, 0.44f * h, 0.13f * h });
        p.mat(shoes);
        p.box(kn, { 0, -0.45f * h, 0.05f * h }, { 0.11f * h, 0.07f * h, 0.25f * h });
    }
    // dress / lungi over the legs
    if (L.style == 1) {
        p.mat(shirt);
        p.draw(MESH_CYLINDER, glm::scale(glm::translate(pelvis, glm::vec3(0, -0.4f * h, 0)), glm::vec3(0.44f * h, 0.82f * h, 0.36f * h)));
    } else if (L.style == 2) {
        p.mat(pants);
        p.draw(MESH_CYLINDER, glm::scale(glm::translate(pelvis, glm::vec3(0, -0.3f * h, 0)), glm::vec3(0.40f * h, 0.62f * h, 0.32f * h)));
    }
    // pelvis + torso
    p.mat(L.style == 1 ? shirt : pants);
    p.box(pelvis, { 0, 0.02f * h, 0 }, { 0.34f * h, 0.16f * h, 0.21f * h });
    p.mat(shirt);
    p.box(pelvis, { 0, 0.34f * h, 0 }, { 0.38f * h, 0.52f * h, 0.22f * h });
    if (L.style == 1) {   // orna / scarf across the chest
        p.mat(plain(glm::vec3(1.0f) - L.shirt * 0.6f, 0.1f, 8));
        glm::mat4 sash = glm::rotate(glm::translate(pelvis, glm::vec3(0, 0.42f * h, 0.12f * h)), 0.6f, Z_AXIS);
        p.box(sash, glm::vec3(0), { 0.1f * h, 0.6f * h, 0.03f * h });
    }
    // head + hair
    p.mat(skin);
    p.box(pelvis, { 0, 0.63f * h, 0 }, { 0.09f * h, 0.08f * h, 0.09f * h });
    p.sphere(pelvis, { 0, 0.74f * h, 0 }, { 0.21f * h, 0.25f * h, 0.23f * h });
    p.mat(plain(L.hair, 0.4f, 32));
    p.sphere(pelvis, { 0, 0.79f * h, -0.02f * h }, { 0.23f * h, 0.17f * h, 0.24f * h });
    if (L.style == 1) p.box(pelvis, { 0, 0.62f * h, -0.1f * h }, { 0.2f * h, 0.3f * h, 0.06f * h });   // long hair
    // arms
    for (int s = 0; s < 2; ++s) {
        float sx = s == 0 ? -0.245f * h : 0.245f * h;
        glm::mat4 sh = glm::rotate(glm::translate(pelvis, glm::vec3(sx, 0.56f * h, 0)), -arm[s], X_AXIS);
        p.mat(shirt);
        p.box(sh, { 0, -0.14f * h, 0 }, { 0.1f * h, 0.3f * h, 0.11f * h });
        glm::mat4 el = glm::rotate(glm::translate(sh, glm::vec3(0, -0.28f * h, 0)), -elbow, X_AXIS);
        p.mat(skin);
        p.box(el, { 0, -0.13f * h, 0 }, { 0.085f * h, 0.27f * h, 0.095f * h });
    }
}

static Look lookOf(const Pedestrian& q) { return { q.shirt, q.pants, q.skin, q.hair, q.style, q.height }; }

static void drawCar(Painter& p, const glm::mat4& root, const Vehicle& v, float time) {
    const float W = v.width, L = v.length;
    glm::vec3 body = v.color;
    Mat paint = Mat(body, glm::vec3(0.7f), 96.0f, PAT_NONE);
    Mat glass = plain({ 0.04f, 0.07f, 0.1f }, 0.9f, 128);
    Mat dark = plain({ 0.06f, 0.06f, 0.07f }, 0.2f, 16);
    p.mat(paint);
    p.box(root, { 0, 0.66f, 0 }, { W, 0.6f, L });                          // lower body
    p.mat(glass);
    p.box(root, { 0, 1.22f, -0.2f }, { W * 0.86f, 0.55f, L * 0.48f });    // cabin glass
    p.mat(paint);
    p.box(root, { 0, 1.51f, -0.2f }, { W * 0.84f, 0.06f, L * 0.44f });    // roof
    p.mat(dark);
    p.box(root, { 0, 0.44f, L * 0.5f }, { W + 0.02f, 0.2f, 0.14f });       // bumpers
    p.box(root, { 0, 0.44f, -L * 0.5f }, { W + 0.02f, 0.2f, 0.14f });
    headTail(p, root, W * 0.5f, L * 0.5f, 0.74f, v.braking);
    if (v.type == VT_TAXI) {
        p.mat(light({ 1, 0.9f, 0.3f }, { 1.2f, 1.0f, 0.3f }, true));
        p.box(root, { 0, 1.66f, -0.2f }, { 0.6f, 0.22f, 0.3f });
        p.mat(plain({ 0.05f, 0.05f, 0.05f }));
        p.box(root, { 0, 0.72f, 0 }, { W + 0.02f, 0.12f, L * 0.7f });     // black side stripe
    }
    if (v.type == VT_POLICE) {
        p.mat(plain({ 0.1f, 0.2f, 0.65f }, 0.5f, 64));
        p.box(root, { 0, 0.72f, 0 }, { W + 0.02f, 0.18f, L * 0.9f });
        bool flash = std::fmod(time * 3.0f, 1.0f) < 0.5f;
        p.mat(light({ 1, 0.1f, 0.1f }, flash ? glm::vec3(3.0f, 0.1f, 0.1f) : glm::vec3(0.2f, 0, 0), false));
        p.box(root, { -0.35f, 1.62f, -0.2f }, { 0.6f, 0.16f, 0.3f });
        p.mat(light({ 0.1f, 0.2f, 1 }, flash ? glm::vec3(0.0f, 0.1f, 0.3f) : glm::vec3(0.1f, 0.4f, 3.0f), false));
        p.box(root, { 0.35f, 1.62f, -0.2f }, { 0.6f, 0.16f, 0.3f });
    }
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            wheel(p, root, sx * (W * 0.5f - 0.1f), sz * (L * 0.5f - 0.85f), 0.33f, 0.24f, v.wheelAngle);
}

static void drawBus(Painter& p, const glm::mat4& root, const Vehicle& v) {
    const float W = v.width, L = v.length;
    p.mat(Mat(v.color, glm::vec3(0.5f), 48.0f, PAT_NONE));
    p.box(root, { 0, 1.75f, 0 }, { W, 2.7f, L });
    p.mat(plain({ 0.04f, 0.07f, 0.1f }, 0.9f, 128));
    p.box(root, { 0, 2.3f, -0.4f }, { W + 0.04f, 0.95f, L - 2.4f });     // side windows
    p.box(root, { 0, 2.1f, L * 0.5f }, { W - 0.3f, 1.3f, 0.06f });        // windscreen
    p.mat(plain({ 0.95f, 0.85f, 0.3f }, 0.4f, 32));
    p.box(root, { 0, 1.3f, 0 }, { W + 0.03f, 0.18f, L - 0.4f });          // colourful stripes
    p.mat(plain({ 0.1f, 0.4f, 0.9f }, 0.4f, 32));
    p.box(root, { 0, 1.05f, 0 }, { W + 0.03f, 0.12f, L - 0.4f });
    p.mat(light({ 0.2f, 0.1f, 0 }, { 1.6f, 0.8f, 0.1f }, true));
    p.box(root, { 0, 2.95f, L * 0.5f + 0.02f }, { 1.6f, 0.3f, 0.05f });   // route display
    headTail(p, root, W * 0.5f, L * 0.5f, 0.75f, v.braking);
    for (int sx = -1; sx <= 1; sx += 2) {
        wheel(p, root, sx * (W * 0.5f - 0.15f), L * 0.5f - 2.0f, 0.5f, 0.3f, v.wheelAngle);
        wheel(p, root, sx * (W * 0.5f - 0.15f), -L * 0.5f + 2.2f, 0.5f, 0.3f, v.wheelAngle);
    }
}

// Bangladeshi cargo truck: orange cab, wooden open bed
static void drawTruck(Painter& p, const glm::mat4& root, const Vehicle& v) {
    const float W = v.width, L = v.length;
    p.mat(Mat(v.color, glm::vec3(0.5f), 48.0f, PAT_NONE));
    p.box(root, { 0, 1.55f, L * 0.5f - 1.0f }, { W, 1.9f, 2.0f });
    p.mat(plain({ 0.04f, 0.07f, 0.1f }, 0.9f, 128));
    p.box(root, { 0, 2.0f, L * 0.5f + 0.01f }, { W - 0.3f, 0.8f, 0.04f });
    p.mat(plain({ 0.1f, 0.5f, 0.2f }, 0.3f, 16));
    p.box(root, { 0, 2.6f, L * 0.5f - 0.6f }, { W + 0.1f, 0.25f, 1.4f }); // painted visor
    p.mat(plain({ 0.12f, 0.12f, 0.12f }, 0.2f, 8));
    p.box(root, { 0, 0.75f, -0.4f }, { W - 0.2f, 0.3f, L - 1.0f });       // chassis
    Mat wood = plain({ 0.50f, 0.32f, 0.16f }, 0.1f, 8);
    p.mat(wood);
    p.box(root, { 0, 1.0f, -1.1f }, { W, 0.15f, L - 2.3f });
    p.box(root, { W * 0.5f - 0.05f, 1.75f, -1.1f }, { 0.1f, 1.4f, L - 2.3f });
    p.box(root, { -W * 0.5f + 0.05f, 1.75f, -1.1f }, { 0.1f, 1.4f, L - 2.3f });
    p.box(root, { 0, 1.75f, -L * 0.5f + 0.05f }, { W, 1.4f, 0.1f });
    p.mat(plain({ 0.75f, 0.68f, 0.5f }, 0.05f, 4));                         // jute sacks
    p.box(root, { 0, 1.55f, -1.2f }, { W - 0.3f, 0.9f, L - 2.8f });
    headTail(p, root, W * 0.5f, L * 0.5f, 0.85f, v.braking);
    for (int sx = -1; sx <= 1; sx += 2) {
        wheel(p, root, sx * (W * 0.5f - 0.15f), L * 0.5f - 1.1f, 0.5f, 0.3f, v.wheelAngle);
        wheel(p, root, sx * (W * 0.5f - 0.15f), -L * 0.5f + 1.5f, 0.5f, 0.3f, v.wheelAngle);
    }
}

// CNG auto-rickshaw: green three-wheeler with a black canvas roof
static void drawCNG(Painter& p, const glm::mat4& root, const Vehicle& v) {
    Mat green = Mat(v.color, glm::vec3(0.5f), 48.0f, PAT_NONE);
    p.mat(green);
    p.box(root, { 0, 0.55f, -0.1f }, { 1.3f, 0.4f, 2.4f });
    p.box(root, { 0, 1.1f, 0.85f }, { 1.2f, 0.75f, 0.35f });               // front cowl
    p.mat(plain({ 0.05f, 0.08f, 0.1f }, 0.9f, 128));
    p.box(root, { 0, 1.45f, 0.95f }, { 1.1f, 0.5f, 0.06f });               // windscreen
    p.mat(green);
    p.box(root, { 0.62f, 1.3f, -0.35f }, { 0.06f, 1.1f, 1.6f });           // side cage (closed panels)
    p.box(root, { -0.62f, 1.3f, -0.35f }, { 0.06f, 1.1f, 1.6f });
    p.mat(plain({ 0.06f, 0.06f, 0.06f }, 0.2f, 8));
    p.box(root, { 0, 1.9f, -0.15f }, { 1.36f, 0.12f, 2.3f });              // canvas roof
    p.box(root, { 0, 1.3f, -1.15f }, { 1.3f, 1.1f, 0.08f });
    p.mat(light({ 1, 0.95f, 0.85f }, { 1.6f, 1.5f, 1.2f }, true));
    p.box(root, { 0, 0.95f, 1.06f }, { 0.25f, 0.18f, 0.05f });
    p.mat(light({ 0.8f, 0.05f, 0.05f }, v.braking ? glm::vec3(2.4f, 0.1f, 0.05f) : glm::vec3(0.6f, 0.02f, 0.02f), false));
    p.box(root, { 0, 0.7f, -1.32f }, { 0.6f, 0.12f, 0.05f });
    wheel(p, root, 0, 1.0f, 0.25f, 0.16f, v.wheelAngle * 1.3f);
    wheel(p, root, -0.6f, -0.85f, 0.25f, 0.16f, v.wheelAngle * 1.3f);
    wheel(p, root, 0.6f, -0.85f, 0.25f, 0.16f, v.wheelAngle * 1.3f);
}

// Cycle rickshaw with a painted hood and a pedalling rider
static void drawRickshaw(Painter& p, const glm::mat4& root, const Vehicle& v) {
    Mat frame = plain({ 0.15f, 0.15f, 0.17f }, 0.5f, 32);
    p.mat(frame);
    p.box(root, { 0, 0.55f, 0.15f }, { 0.08f, 0.08f, 1.8f });             // main beam
    p.box(root, { 0, 0.45f, -0.75f }, { 1.05f, 0.06f, 0.06f });            // rear axle
    p.box(root, { 0, 0.9f, 1.05f }, { 0.06f, 0.8f, 0.06f });               // fork
    p.box(root, { 0, 1.32f, 0.98f }, { 0.55f, 0.05f, 0.05f });             // handlebar
    p.mat(Mat(v.color, glm::vec3(0.5f), 48.0f, PAT_NONE));
    p.box(root, { 0, 0.85f, -0.6f }, { 1.0f, 0.25f, 0.65f });              // passenger seat
    p.box(root, { 0, 1.15f, -0.95f }, { 1.0f, 0.6f, 0.1f });               // backrest
    // folding hood: three tilted panels
    Mat hood = plain(glm::vec3(1.0f) - v.color * 0.7f, 0.3f, 16);
    p.mat(hood);
    for (int k = 0; k < 3; ++k) {
        float a = glm::radians(-20.0f + k * 35.0f);
        glm::mat4 m = glm::rotate(glm::translate(root, glm::vec3(0, 1.1f, -0.7f)), a, X_AXIS);
        p.box(m, { 0, 0.75f, 0 }, { 1.05f, 0.06f, 0.55f });
    }
    wheel(p, root, -0.5f, -0.75f, 0.35f, 0.05f, v.wheelAngle, true);
    wheel(p, root, 0.5f, -0.75f, 0.35f, 0.05f, v.wheelAngle, true);
    wheel(p, root, 0, 1.05f, 0.35f, 0.05f, v.wheelAngle, true);
    // rider on the saddle: legs follow the crank angle
    p.mat(frame);
    p.box(root, { 0, 1.02f, 0.45f }, { 0.2f, 0.06f, 0.3f });
    Look rider{ { 0.85f, 0.85f, 0.8f }, { 0.2f, 0.3f, 0.45f }, { 0.4f, 0.26f, 0.16f }, { 0.04f, 0.03f, 0.02f }, 2, 0.95f };
    float crank = glm::radians(v.wheelAngle * 1.5f);
    float legF[2] = { 1.2f + 0.35f * std::sin(crank), 1.2f - 0.35f * std::sin(crank) };
    float knee[2] = { 1.3f - 0.35f * std::cos(crank), 1.3f + 0.35f * std::cos(crank) };
    float arm[2] = { 0.9f, 0.9f };
    glm::mat4 seat = glm::rotate(glm::translate(root, glm::vec3(0, 0, 0.42f)), glm::radians(10.0f), X_AXIS);
    drawPerson(p, seat, rider, 1.05f, legF, knee, arm, 0.4f);
}

// NightWatch patrol truck (the original project's Bezier vehicle)
static void drawPatrol(Painter& p, const glm::mat4& root, const Vehicle& v, float time) {
    p.mat(plain({ 0.18f, 0.20f, 0.22f }, 0.4f, 32));
    p.box(root, { 0, 0.65f, 0 }, { 2.2f, 0.45f, 5.4f });
    p.mat(Mat(v.color, glm::vec3(0.6f), 64.0f, PAT_NONE));
    p.box(root, { 0, 1.6f, 1.4f }, { 2.1f, 1.5f, 2.0f });
    p.mat(plain({ 0.04f, 0.08f, 0.12f }, 0.9f, 128));
    p.box(root, { 0, 1.75f, 2.42f }, { 1.8f, 0.9f, 0.08f });
    p.mat(plain({ 0.65f, 0.68f, 0.72f }, 0.4f, 32));
    p.box(root, { 0, 1.95f, -1.0f }, { 2.3f, 2.2f, 3.2f });
    p.mat(plain({ 0.95f, 0.85f, 0.15f }, 0.8f, 64));
    p.box(root, { 0, 1.95f, -1.0f }, { 2.34f, 0.35f, 3.1f });
    bool flash = std::fmod(time * 2.5f, 1.0f) < 0.5f;
    p.mat(light({ 1.0f, 0.6f, 0.1f }, flash ? glm::vec3(3.0f, 1.6f, 0.1f) : glm::vec3(0.3f, 0.15f, 0.0f), false));
    p.box(root, { 0, 2.45f, 1.4f }, { 1.2f, 0.18f, 0.35f });
    headTail(p, root, 1.1f, 2.7f, 0.9f, v.braking);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            wheel(p, root, sx * 1.18f, sz * 1.45f, 0.55f, 0.42f, v.wheelAngle);
}

void TownScene::renderVehicles(Painter& p, const TrafficSystem& traffic, float time) const {
    for (const Vehicle& v : traffic.vehicles) {
        if (glm::length(v.pos - cullCenter) > 260.0f) continue;
        glm::mat4 root = glm::rotate(glm::translate(glm::mat4(1.0f), v.pos), glm::radians(v.yawDeg), Y_AXIS);
        switch (v.type) {
            case VT_BUS:      drawBus(p, root, v); break;
            case VT_TRUCK:    drawTruck(p, root, v); break;
            case VT_CNG:      drawCNG(p, root, v); break;
            case VT_RICKSHAW: drawRickshaw(p, root, v); break;
            case VT_PATROL:   drawPatrol(p, root, v, time); break;
            default:          drawCar(p, root, v, time); break;
        }
    }
}

void TownScene::renderPedestrians(Painter& p, const TrafficSystem& traffic) const {
    // people further than 60 m cast shadows too small to see: skip them in the shadow pass
    const float maxDist = p.shadowPass() ? 60.0f : 120.0f;
    for (const Pedestrian& q : traffic.pedestrians) {
        if (glm::length(q.pos - cullCenter) > maxDist) continue;
        glm::mat4 root = glm::rotate(glm::translate(glm::mat4(1.0f), q.pos), glm::radians(q.yawDeg), Y_AXIS);
        float amp = glm::clamp(q.speed / 1.2f, 0.0f, 1.0f);       // walk cycle fades out when standing
        float sw = std::sin(q.phase) * 0.45f * amp;
        float legF[2] = { sw, -sw };
        float knee[2] = { (0.1f + 0.7f * std::max(0.0f, std::sin(q.phase + 1.6f))) * amp,
                          (0.1f + 0.7f * std::max(0.0f, std::sin(q.phase + 3.14159f + 1.6f))) * amp };
        float arm[2] = { -sw * 0.9f, sw * 0.9f };
        float bob = 0.03f * std::fabs(std::cos(q.phase)) * amp;
        drawPerson(p, root, lookOf(q), 0.92f * q.height + bob, legF, knee, arm, 0.2f + 0.2f * amp);
    }
}

// ---------------------------------------------------------------------------
// CCTV: mast + 4-DOF chain (Base -> Yaw joint -> Pitch joint -> Lens)
// ---------------------------------------------------------------------------
void TownScene::renderCCTV(Painter& p, const CCTVKinematicChain& cctv, float time) const {
    glm::vec3 b = cctv.basePosition;
    p.mat(plain({ 0.25f, 0.27f, 0.30f }, 0.5f, 64));
    p.cyl(glm::mat4(1.0f), glm::vec3(b.x, b.y * 0.5f, b.z), 0.2f, b.y);
    p.mat(plain({ 0.30f, 0.32f, 0.35f }, 0.4f, 32));
    p.box(cctv.getBaseMatrix(), glm::vec3(0), { 0.7f, 0.2f, 0.7f });
    p.mat(plain({ 0.45f, 0.48f, 0.50f }, 0.6f, 64));
    p.draw(MESH_CYLINDER, glm::scale(cctv.getYawJointMatrix(), glm::vec3(0.5f)));
    glm::mat4 pitch = cctv.getPitchJointMatrix();
    p.mat(plain({ 0.35f, 0.38f, 0.40f }, 0.5f, 32));
    p.box(pitch, glm::vec3(0), { 0.65f, 0.35f, 0.35f });
    glm::mat4 head = glm::translate(pitch, glm::vec3(0, 0, 0.35f));
    p.mat(plain({ 0.65f, 0.68f, 0.72f }, 0.7f, 64));
    p.box(head, glm::vec3(0), { 0.55f, 0.5f, 0.9f });
    p.mat(plain({ 0.25f, 0.25f, 0.28f }, 0.4f, 32));
    p.box(head, { 0, 0.28f, 0.08f }, { 0.62f, 0.08f, 1.1f });             // sun hood
    glm::mat4 lens = cctv.getLensMatrix();
    p.mat(plain({ 0.15f, 0.15f, 0.15f }, 0.9f, 128));
    p.draw(MESH_CYLINDER, glm::scale(glm::rotate(lens, glm::radians(90.0f), X_AXIS), glm::vec3(0.35f, 0.25f, 0.35f)));
    p.mat(plain({ 0.1f, 0.3f, 0.5f }, 1.0f, 128));
    p.sphere(lens, { 0, 0, 0.13f }, { 0.28f, 0.28f, 0.05f });
    bool blink = std::fmod(time, 1.0f) < 0.5f;
    p.mat(light({ 1, 0.1f, 0.1f }, blink ? glm::vec3(1.5f, 0, 0) : glm::vec3(0.0f), false));
    p.sphere(head, { 0.2f, -0.15f, 0.46f }, glm::vec3(0.08f));
    p.mat(plain({ 0.2f, 0.25f, 0.3f }, 0.5f, 32));
    p.box(head, { 0, -0.28f, 0.35f }, { 0.45f, 0.18f, 0.3f });            // IR illuminator
}

// Park fountain: water drops on parabolic arcs  y = v t - g t^2 / 2
void TownScene::renderFountain(Painter& p, float time) const {
    glm::vec3 c(-20.0f, KERB_H, 20.0f);
    if (glm::length(c - cullCenter) > 150.0f) return;
    Mat water = plain({ 0.6f, 0.8f, 0.95f }, 1.0f, 128);
    water.emissive = glm::vec3(0.08f, 0.12f, 0.16f);
    p.mat(water);
    float jet = 2.2f + 0.25f * std::sin(time * 3.0f);
    p.cyl(glm::mat4(1.0f), c + glm::vec3(0, 2.0f + jet * 0.5f, 0), 0.07f, jet);
    for (int k = 0; k < 6; ++k) {
        float a = k * 1.0472f;
        glm::vec3 dir(std::cos(a), 0, std::sin(a));
        for (int d = 0; d < 5; ++d) {
            float t = std::fmod(time * 0.8f + d * 0.2f, 1.0f);       // 0..1 along the arc
            float r = 0.6f + 2.4f * t;
            float y = 2.0f + 3.2f * t - 3.0f * t * t;
            p.sphere(glm::mat4(1.0f), c + dir * r + glm::vec3(0, y, 0), glm::vec3(0.14f));
        }
    }
}

// G key: the patrol car's route; markers along it plus each corner's Bezier
// control polygon P0-P1-P2-P3 (yellow end points, red handles).
void TownScene::renderRouteGuide(Painter& p, const TrafficSystem& traffic) const {
    if (traffic.vehicles.empty()) return;
    const Route& r = traffic.routes[traffic.vehicles[traffic.patrolVehicle].route];
    p.mat(light({ 0.2f, 0.7f, 1.0f }, { 0.1f, 0.6f, 1.2f }, false));
    for (float s = 0.0f; s < r.length; s += 3.0f)
        p.box(r.sample(s) + glm::vec3(0, 0.1f, 0), { 0.3f, 0.06f, 0.3f });
    for (const BezierSegment& b : r.turns) {
        const glm::vec3 pts[4] = { b.p0, b.p1, b.p2, b.p3 };
        for (int i = 0; i < 4; ++i) {
            p.mat(light(i == 0 || i == 3 ? glm::vec3(1, 0.9f, 0.1f) : glm::vec3(1, 0.1f, 0.1f),
                        i == 0 || i == 3 ? glm::vec3(2.0f, 1.6f, 0.1f) : glm::vec3(2.0f, 0.1f, 0.1f), false));
            p.sphere(glm::mat4(1.0f), pts[i] + glm::vec3(0, 0.4f, 0), glm::vec3(0.45f));
            if (i < 3) {   // control polygon edge
                glm::vec3 a = pts[i], e = pts[i + 1];
                glm::vec3 mid = (a + e) * 0.5f + glm::vec3(0, 0.4f, 0);
                float len = glm::length(e - a);
                if (len < 1e-3f) continue;
                float yaw = std::atan2(e.x - a.x, e.z - a.z);
                p.mat(plain({ 0.9f, 0.9f, 0.9f }));
                p.draw(MESH_CUBE, glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), mid), yaw, Y_AXIS), glm::vec3(0.06f, 0.06f, len)));
            }
        }
    }
}
