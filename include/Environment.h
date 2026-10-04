#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "Shader.h"

// Time of day, sky colours and every light in the scene.
//
// sunAngle: 0 = sunrise (06:00), 90 = noon, 180 = sunset, 270 = midnight.
// From it we derive the sun/moon direction and colour, the sky gradient,
// fog, exposure and how bright the lamps / headlights are.
//
// The town has far more lamps and headlights than a forward shader can loop
// over, so each frame only the nearest MAX_POINTS lamps and MAX_SPOTS spot
// lights (CCTV IR first) are sent to the shader.
struct SpotSource {
    glm::vec3 pos, dir, color;
    glm::vec2 cone;   // cos(inner), cos(outer)
};

class Environment {
public:
    float sunAngle;
    bool autoCycle;
    bool shadowsEnabled;

    // L key cycles: 0 all, 1 sun/moon only, 2 lamps only, 3 spotlights only
    int lightState;

    // Derived every frame by update()
    float dayFactor;            // 0 = night, 1 = full day
    glm::vec3 toSun, toMoon;
    glm::vec3 sunColor;         // display space
    glm::vec3 zenith, horizon;  // display space sky gradient (horizon = fog colour)

    static const int MAX_POINTS = 16;   // must match lighting.glsl
    static const int MAX_SPOTS = 12;

    Environment();

    void update(float dt);   // sun, sky, fog, exposure
    // lamps: every street lamp bulb; spots[0] is always kept (CCTV IR); viewPos picks the nearest
    void setLocalLights(const std::vector<glm::vec3>& lamps, const std::vector<SpotSource>& spots, const glm::vec3& viewPos);
    void setShadowCenter(const glm::vec3& c) { shadowCenter = c; }
    void cycleLights();
    const char* lightStateName() const;
    std::string clockString() const;   // "14:20"

    glm::mat4 lightSpaceMatrix() const;
    void applyToSceneShader(const Shader& s) const;
    void applyToSkyShader(const Shader& s, const glm::mat4& view, const glm::mat4& proj, float time) const;
    float lampGlow() const;             // emissive strength of bulbs / headlights

private:
    glm::vec3 skyDir, skyColorLinear, hemiSky, hemiGround;
    float exposure, fogDensity;
    glm::vec3 shadowCenter;
    int pointCount, spotCount;
    glm::vec3 pointPos[MAX_POINTS], pointColor[MAX_POINTS];
    glm::vec3 spotPos[MAX_SPOTS], spotDir[MAX_SPOTS], spotColor[MAX_SPOTS];
    glm::vec2 spotCone[MAX_SPOTS];
};
