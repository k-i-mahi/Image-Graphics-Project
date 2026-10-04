#include "Environment.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static float smooth01(float e0, float e1, float x) {
    float t = glm::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static glm::vec3 toLinear(const glm::vec3& c) {
    return glm::pow(c, glm::vec3(2.2f));
}

Environment::Environment()
    : sunAngle(55.0f),
      autoCycle(false),
      shadowsEnabled(true),
      lightState(0),
      dayFactor(1.0f),
      toSun(0.0f, 1.0f, 0.0f),
      toMoon(glm::normalize(glm::vec3(0.35f, 0.75f, -0.55f))),
      sunColor(1.0f),
      zenith(0.0f),
      horizon(0.0f),
      skyDir(0.0f, -1.0f, 0.0f),
      skyColorLinear(0.0f),
      hemiSky(0.0f),
      hemiGround(0.0f),
      exposure(1.0f),
      fogDensity(0.005f),
      shadowCenter(0.0f),
      pointCount(0),
      spotCount(0) {}

void Environment::update(float dt) {
    if (autoCycle) sunAngle = std::fmod(sunAngle + 12.0f * dt, 360.0f);

    // ---- Sun position & colour ----
    float a = glm::radians(sunAngle);
    toSun = glm::normalize(glm::vec3(std::cos(a), std::sin(a), -0.6f));   // arcs over the far (-z) side
    float elevation = std::sin(a);
    dayFactor = smooth01(-0.08f, 0.22f, elevation);
    sunColor = glm::mix(glm::vec3(1.0f, 0.48f, 0.22f), glm::vec3(1.0f, 0.96f, 0.88f), smooth01(0.0f, 0.45f, elevation));

    // ---- Sky gradient (display space) ----
    glm::vec3 dayZenith(0.16f, 0.36f, 0.74f), dayHorizon(0.64f, 0.76f, 0.9f);
    glm::vec3 nightZenith(0.006f, 0.009f, 0.028f), nightHorizon(0.035f, 0.045f, 0.085f);
    zenith = glm::mix(nightZenith, dayZenith, dayFactor);
    horizon = glm::mix(nightHorizon, dayHorizon, dayFactor);
    float glow = std::exp(-(elevation * 4.5f) * (elevation * 4.5f));           // strongest at the horizon
    horizon = glm::mix(horizon, glm::vec3(0.95f, 0.5f, 0.27f), 0.75f * glow);
    zenith = glm::mix(zenith, glm::vec3(0.22f, 0.26f, 0.5f), 0.3f * glow);

    // ---- Directional light: sun above the horizon, moon below ----
    if (elevation > 0.0f) {
        skyDir = -toSun;
        skyColorLinear = toLinear(sunColor) * 3.2f * smooth01(-0.02f, 0.2f, elevation);
    } else {
        skyDir = -toMoon;
        skyColorLinear = glm::vec3(0.05f, 0.07f, 0.12f) * (1.0f - smooth01(-0.2f, 0.05f, elevation));
    }
    hemiSky = glm::mix(glm::vec3(0.012f, 0.016f, 0.03f), toLinear(glm::mix(horizon, zenith, 0.5f)) * 0.9f, dayFactor);
    hemiGround = glm::mix(glm::vec3(0.003f), glm::vec3(0.07f, 0.06f, 0.04f), dayFactor);
    exposure = glm::mix(2.6f, 1.0f, dayFactor);
    fogDensity = glm::mix(0.0075f, 0.0042f, dayFactor);
}

void Environment::setLocalLights(const std::vector<glm::vec3>& lamps, const std::vector<SpotSource>& spots,
                                 const glm::vec3& viewPos) {
    // ---- Street lamps (sodium, dim in daylight): nearest MAX_POINTS to the viewer ----
    std::vector<std::pair<float, int>> order;
    for (size_t i = 0; i < lamps.size(); ++i) order.push_back({ glm::length(lamps[i] - viewPos), static_cast<int>(i) });
    std::sort(order.begin(), order.end());
    pointCount = std::min<int>(MAX_POINTS, static_cast<int>(order.size()));
    for (int i = 0; i < pointCount; ++i) {
        pointPos[i] = lamps[order[i].second];
        pointColor[i] = glm::vec3(1.0f, 0.52f, 0.16f) * 3.5f * lampGlow();
    }

    // ---- Spotlights: CCTV IR (always) + nearest headlights ----
    order.clear();
    for (size_t i = 1; i < spots.size(); ++i) order.push_back({ glm::length(spots[i].pos - viewPos), static_cast<int>(i) });
    std::sort(order.begin(), order.end());
    spotCount = 0;
    auto push = [&](const SpotSource& s) {
        spotPos[spotCount] = s.pos;
        spotDir[spotCount] = s.dir;
        spotColor[spotCount] = s.color;
        spotCone[spotCount] = s.cone;
        ++spotCount;
    };
    if (!spots.empty()) push(spots[0]);
    for (size_t i = 0; i < order.size() && spotCount < MAX_SPOTS; ++i) push(spots[order[i].second]);
}

float Environment::lampGlow() const {
    return 1.0f - 0.9f * dayFactor;
}

void Environment::cycleLights() {
    lightState = (lightState + 1) % 4;
}

const char* Environment::lightStateName() const {
    switch (lightState) {
        case 1:  return "Sun/Moon only";
        case 2:  return "Street lamps only";
        case 3:  return "Spotlights only (CCTV IR + headlights)";
        default: return "All lights";
    }
}

std::string Environment::clockString() const {
    float hours = std::fmod(6.0f + sunAngle / 15.0f, 24.0f);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", static_cast<int>(hours), static_cast<int>(std::fmod(hours, 1.0f) * 60.0f));
    return buf;
}

glm::mat4 Environment::lightSpaceMatrix() const {
    // Orthographic box (150 m wide) that follows the camera, looking along the light direction.
    // Its centre is snapped to whole shadow-map texels so shadows do not shimmer as it moves.
    const float HALF = 75.0f, RES = 2048.0f;
    glm::vec3 up = std::fabs(skyDir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    glm::mat4 view = glm::lookAt(-skyDir * 150.0f, glm::vec3(0.0f), up);
    glm::vec3 c = glm::vec3(view * glm::vec4(shadowCenter, 1.0f));
    float texel = 2.0f * HALF / RES;
    c.x = std::floor(c.x / texel) * texel;
    c.y = std::floor(c.y / texel) * texel;
    glm::mat4 proj = glm::ortho(c.x - HALF, c.x + HALF, c.y - HALF, c.y + HALF, -c.z - 200.0f, -c.z + 200.0f);
    return proj * view;
}

void Environment::applyToSceneShader(const Shader& s) const {
    bool skyOn = lightState == 0 || lightState == 1;
    bool lampsOn = lightState == 0 || lightState == 2;
    bool spotsOn = lightState == 0 || lightState == 3;

    s.setBool("uSkyEnabled", skyOn);
    s.setVec3("uSkyDir", skyDir);
    s.setVec3("uSkyColor", skyColorLinear);
    s.setVec3("uHemiSky", hemiSky);
    s.setVec3("uHemiGround", hemiGround);

    s.setInt("uPointCount", lampsOn ? pointCount : 0);
    if (pointCount > 0) {
        glUniform3fv(s.loc("uPointPos"), pointCount, &pointPos[0].x);
        glUniform3fv(s.loc("uPointColor"), pointCount, &pointColor[0].x);
    }
    s.setVec3("uPointAtten", 1.0f, 0.045f, 0.0075f);
    s.setFloat("uPointRange", 22.0f);

    s.setInt("uSpotCount", spotsOn ? spotCount : 0);
    if (spotCount > 0) {
        glUniform3fv(s.loc("uSpotPos"), spotCount, &spotPos[0].x);
        glUniform3fv(s.loc("uSpotDir"), spotCount, &spotDir[0].x);
        glUniform3fv(s.loc("uSpotColor"), spotCount, &spotColor[0].x);
        glUniform2fv(s.loc("uSpotCone"), spotCount, &spotCone[0].x);
    }
    s.setVec3("uSpotAtten", 1.0f, 0.02f, 0.002f);
    s.setFloat("uSpotRange", 40.0f);

    s.setMat4("uLightSpace", lightSpaceMatrix());
    s.setBool("uShadowsEnabled", shadowsEnabled);
    s.setFloat("uExposure", exposure);
    s.setFloat("uNight", 1.0f - dayFactor);
    s.setVec3("uFogColor", horizon);
    s.setVec3("uSkyZenith", zenith);
    s.setFloat("uFogDensity", fogDensity);
}

void Environment::applyToSkyShader(const Shader& s, const glm::mat4& view, const glm::mat4& proj, float time) const {
    glm::mat4 rotOnly = glm::mat4(glm::mat3(view));
    s.setMat4("uInvViewProjRot", glm::inverse(proj * rotOnly));
    s.setVec3("uZenith", zenith);
    s.setVec3("uHorizon", horizon);
    s.setVec3("uToSun", toSun);
    s.setVec3("uSunColor", sunColor);
    s.setFloat("uSunVisible", smooth01(-0.12f, 0.02f, toSun.y));
    s.setVec3("uToMoon", toMoon);
    s.setFloat("uDay", dayFactor);
    s.setFloat("uTime", time);
}
