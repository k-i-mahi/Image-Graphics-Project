// ---------------------------------------------------------------------------
// Shared lighting model (included by scene.vert for Gouraud and scene.frag
// for Phong / Flat). All colours are linear; values come from Environment.cpp.
//
//   ambient  : hemisphere light  mix(ground, sky, N.y)
//   sky      : directional sun (day) or moon (night), shadowed
//   points   : nearest street lamps, quadratic attenuation 1 / (c + l*d + q*d^2)
//   spots    : CCTV IR illuminator + nearest vehicle headlights, smooth cone falloff
//   specular : Blinn-Phong
// ---------------------------------------------------------------------------

#define MAX_POINT_LIGHTS 16
#define MAX_SPOT_LIGHTS 12

uniform bool uSkyEnabled;
uniform vec3 uSkyDir;          // direction the light travels
uniform vec3 uSkyColor;
uniform vec3 uHemiSky;
uniform vec3 uHemiGround;

uniform int  uPointCount;
uniform vec3 uPointPos[MAX_POINT_LIGHTS];
uniform vec3 uPointColor[MAX_POINT_LIGHTS];
uniform vec3 uPointAtten;      // constant, linear, quadratic
uniform float uPointRange;     // light fades smoothly to zero at this distance

uniform int  uSpotCount;
uniform vec3 uSpotPos[MAX_SPOT_LIGHTS];
uniform vec3 uSpotDir[MAX_SPOT_LIGHTS];
uniform vec3 uSpotColor[MAX_SPOT_LIGHTS];
uniform vec2 uSpotCone[MAX_SPOT_LIGHTS];   // cos(inner), cos(outer)
uniform vec3 uSpotAtten;
uniform float uSpotRange;

uniform float uMatShininess;

float blinnPhong(vec3 N, vec3 L, vec3 V) {
    vec3 H = normalize(L + V);
    float n = uMatShininess * 2.0;
    return pow(max(dot(N, H), 0.0), n) * (n + 8.0) / 64.0;   // roughly energy-normalized
}

// Smooth range window so a light has a finite reach: (1 - (d/R)^4)^2
float rangeWindow(float d, float R) {
    float x = clamp(1.0 - pow(d / R, 4.0), 0.0, 1.0);
    return x * x;
}

vec3 hemisphereAmbient(vec3 N) {
    return mix(uHemiGround, uHemiSky, N.y * 0.5 + 0.5);
}

// Directional sky light, before shadowing
void skyLight(vec3 N, vec3 V, out vec3 diffuse, out vec3 specular) {
    diffuse = vec3(0.0);
    specular = vec3(0.0);
    if (!uSkyEnabled) return;
    vec3 L = normalize(-uSkyDir);
    float ndl = max(dot(N, L), 0.0);
    diffuse = uSkyColor * ndl;
    specular = uSkyColor * blinnPhong(N, L, V) * ndl;
}

// Lamps and spotlights
void localLights(vec3 N, vec3 P, vec3 V, out vec3 diffuse, out vec3 specular) {
    diffuse = vec3(0.0);
    specular = vec3(0.0);

    for (int i = 0; i < uPointCount; ++i) {
        vec3 toL = uPointPos[i] - P;
        float d = length(toL);
        vec3 L = toL / d;
        float att = rangeWindow(d, uPointRange) / (uPointAtten.x + uPointAtten.y * d + uPointAtten.z * d * d);
        float ndl = max(dot(N, L), 0.0);
        diffuse += uPointColor[i] * ndl * att;
        specular += uPointColor[i] * blinnPhong(N, L, V) * ndl * att;
    }

    for (int i = 0; i < uSpotCount; ++i) {
        vec3 toL = uSpotPos[i] - P;
        float d = length(toL);
        vec3 L = toL / d;
        float theta = dot(L, normalize(-uSpotDir[i]));
        float cone = clamp((theta - uSpotCone[i].y) / (uSpotCone[i].x - uSpotCone[i].y), 0.0, 1.0);
        cone *= cone;
        float att = cone * rangeWindow(d, uSpotRange) / (uSpotAtten.x + uSpotAtten.y * d + uSpotAtten.z * d * d);
        float ndl = max(dot(N, L), 0.0);
        diffuse += uSpotColor[i] * ndl * att;
        specular += uSpotColor[i] * blinnPhong(N, L, V) * ndl * att;
    }
}
