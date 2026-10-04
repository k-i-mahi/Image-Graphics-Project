#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in vec3 gAmbientLocal;
in vec3 gSunDiffuse;
in vec3 gSpecLocal;
in vec3 gSunSpec;

uniform int uShadingModel; // 0 = Phong, 1 = Gouraud, 2 = Flat
uniform vec3 uViewPos;
uniform float uTime;

// Material (colours authored in sRGB, converted to linear here)
uniform vec3 uMatDiffuse;
uniform vec3 uMatSpecular;
uniform vec3 uMatEmissive;
uniform int uMatPattern;   // see applyPattern()

// Shadow map (sky light)
uniform sampler2D uShadowMap;
uniform mat4 uLightSpace;
uniform bool uShadowsEnabled;

// Camera response + atmosphere
uniform float uExposure;
uniform float uNight;       // 0 = day, 1 = night (lit windows)
uniform vec3 uFogColor;     // display-space, equals the sky horizon colour
uniform vec3 uSkyZenith;    // display-space sky colour overhead (for reflections)
uniform float uFogDensity;

#include "common.glsl"
#include "lighting.glsl"

// ---------------------------------------------------------------------------
// Procedural surface patterns (no texture files: everything from world position)
// ---------------------------------------------------------------------------
#define PAT_NONE      0
#define PAT_GROUND    1   // asphalt under the town, grass + dirt outside
#define PAT_CONCRETE  2
#define PAT_BRICK     3
#define PAT_METAL     4   // corrugated steel
#define PAT_FACADE    5   // wall with a grid of windows (lit at night), cell size = uFacadeCell
#define PAT_FOLIAGE   6
#define PAT_BARK      7
#define PAT_ROAD      8   // lanes, centre line, zebra crossings, stop lines
#define PAT_PAVEMENT  9   // footpath tiles + painted kerb
#define PAT_GRASS     10  // mown lawn
#define PAT_PLAZA     11  // stone slabs
#define PAT_WATER     12  // animated ripples
#define PAT_ROOFTILE  13  // clay roof tiles
#define PAT_GLASS     14  // curtain-wall office tower
#define PAT_PADDY     15  // rice field: planted rows with water between them

uniform vec2 uFacadeCell;  // window grid cell (metres) for PAT_FACADE

// Town street grid (must match Town.h)
#define GRID       40.0   // distance between road centre lines
#define TOWN_EDGE  120.0  // outermost road centre lines at +-120
#define ROAD_HALF  4.5    // half road width (two lanes)

// Planar coordinates on a surface, picked by its dominant normal axis
vec2 surfaceUV(vec3 P, vec3 N) {
    vec3 a = abs(N);
    if (a.x > a.y && a.x > a.z) return vec2(P.z, P.y);
    if (a.z > a.y) return vec2(P.x, P.y);
    return P.xz;
}

vec3 asphalt(vec2 p) {
    float n = fbm(p * 0.6);
    float speck = step(0.93, hash12(floor(p * 40.0)));
    return vec3(0.040, 0.042, 0.046) * (0.7 + 0.6 * n) + speck * 0.025;
}

// Paint on one road arm.
//   along  : coordinate along the road      across : signed offset from the centre line
//   toJ    : distance to the nearest junction centre along the road
//   approach : true on the lane that drives towards that junction (left-hand traffic)
float roadPaint(float along, float across, float toJ, bool junction, bool approach) {
    float a = abs(across);
    float paint = 0.0;
    bool nearJ = junction && toJ < ROAD_HALF + 4.8;
    // dashed centre line (3 m dash, 3 m gap), stopped short of the junction
    if (!nearJ) paint = max(paint, step(a, 0.09) * step(fract(along / 6.0), 0.5));
    // solid edge lines
    paint = max(paint, step(abs(a - (ROAD_HALF - 0.35)), 0.07));
    if (junction) {
        // zebra crossing: 0.5 m stripes across the full carriageway, 3 m deep
        float zebra = step(ROAD_HALF + 0.8, toJ) * step(toJ, ROAD_HALF + 3.8) * step(a, ROAD_HALF - 0.5)
                    * step(fract(across / 1.0), 0.5);
        paint = max(paint, zebra);
        // stop line on the approaching lane only
        if (approach) paint = max(paint, step(ROAD_HALF + 4.2, toJ) * step(toJ, ROAD_HALF + 4.6) * step(a, ROAD_HALF - 0.4));
    }
    return paint;
}

void applyPattern(vec3 P, inout vec3 N, inout vec3 albedo, inout vec3 emissive, inout float specScale) {
    if (uMatPattern == PAT_GROUND) {
        bool town = max(abs(P.x), abs(P.z)) < TOWN_EDGE + ROAD_HALF + 0.5;
        if (town) {
            albedo = asphalt(P.xz);
            specScale = 0.3;
        } else {
            float n = fbm(P.xz * 0.12);
            vec3 grass = mix(vec3(0.035, 0.09, 0.020), vec3(0.10, 0.15, 0.04), n);
            grass *= 0.8 + 0.4 * hash12(floor(P.xz * 18.0));
            float dirt = smoothstep(0.62, 0.78, fbm(P.xz * 0.04 + 7.0));
            albedo = mix(grass, vec3(0.10, 0.075, 0.045), dirt);
            specScale = 0.05;
        }
    }
    else if (uMatPattern == PAT_ROAD) {
        albedo = asphalt(P.xz);
        specScale = 0.3;
        vec2 c = floor(P.xz / GRID + 0.5) * GRID;  // nearest junction (x, z)
        vec2 d = P.xz - c;
        vec2 ad = abs(d);
        bool jx = abs(c.x) <= TOWN_EDGE + 0.1;     // a junction exists at this x
        bool jz = abs(c.y) <= TOWN_EDGE + 0.1;
        float paint = 0.0;
        if (ad.x < ROAD_HALF && ad.y < ROAD_HALF && jx && jz) {
            paint = 0.0;                                                   // junction box
        } else if (ad.y < ROAD_HALF) {                                     // road along X
            bool approach = d.y * sign(d.x) > 0.0;
            paint = roadPaint(P.x, d.y, ad.x, jx && jz, approach);
            albedo *= 1.0 - 0.18 * smoothstep(0.6, 0.0, abs(ad.y - 2.0));   // darker tyre tracks
        } else if (ad.x < ROAD_HALF) {                                     // road along Z
            bool approach = -d.x * sign(d.y) > 0.0;
            paint = roadPaint(P.z, d.x, ad.y, jx && jz, approach);
            albedo *= 1.0 - 0.18 * smoothstep(0.6, 0.0, abs(ad.x - 2.0));
        }
        float worn = 0.75 + 0.25 * vnoise(P.xz * 3.0);
        albedo = mix(albedo, vec3(0.62) * worn, paint);
    }
    else if (uMatPattern == PAT_PAVEMENT) {
        vec2 c = floor(P.xz / GRID + 0.5) * GRID;
        vec2 ad = abs(P.xz - c);
        float kerb = min(ad.x, ad.y) - ROAD_HALF;          // metres from the road edge
        if (kerb < 0.3) {
            // painted kerb: black / white 1 m blocks
            float along = ad.y < ad.x ? P.x : P.z;
            albedo = fract(along / 2.0) < 0.5 ? vec3(0.60) : vec3(0.05);
        } else {
            vec2 t = P.xz / 0.6;
            vec2 f = abs(fract(t) - 0.5);
            float joint = step(0.46, max(f.x, f.y));
            vec3 tile = vec3(0.27, 0.24, 0.22) * (0.85 + 0.3 * hash12(floor(t)));
            albedo = mix(tile * (0.85 + 0.25 * fbm(P.xz * 0.5)), vec3(0.16), joint);
        }
        specScale = 0.15;
    }
    else if (uMatPattern == PAT_GRASS) {
        float n = fbm(P.xz * 0.35);
        vec3 lawn = mix(vec3(0.05, 0.14, 0.03), vec3(0.11, 0.21, 0.05), n);
        float stripes = step(0.5, fract(P.x / 3.0));          // mowing stripes
        albedo = lawn * (0.88 + 0.16 * stripes) * (0.85 + 0.3 * hash12(floor(P.xz * 20.0)));
        specScale = 0.05;
    }
    else if (uMatPattern == PAT_PLAZA) {
        vec2 uv = surfaceUV(P, N);
        vec2 t = uv / vec2(1.2, 0.8);
        t.x += step(1.0, mod(floor(t.y), 2.0)) * 0.5;
        vec2 f = abs(fract(t) - 0.5);
        float joint = step(0.47, max(f.x, f.y));
        vec3 stone = albedo * (0.82 + 0.3 * hash12(floor(t))) * (0.9 + 0.2 * fbm(uv * 0.7));
        albedo = mix(stone, albedo * 0.45, joint);
        specScale = 0.2;
    }
    else if (uMatPattern == PAT_WATER) {
        vec2 q = P.xz * 1.6;
        float h1 = vnoise(q + vec2(uTime * 0.6, uTime * 0.4));
        float h2 = vnoise(q * 1.7 - vec2(uTime * 0.5, uTime * 0.7));
        N = normalize(N + vec3(h1 - 0.5, 0.0, h2 - 0.5) * 0.35);
        albedo = vec3(0.02, 0.07, 0.10);
        specScale = 5.0;
    }
    else if (uMatPattern == PAT_PADDY) {
        float row = abs(fract(P.z / 0.7) - 0.5) * 2.0;                  // 0 at a row of plants, 1 between rows
        float plants = smoothstep(0.92, 0.45, row) * (0.75 + 0.25 * vnoise(P.xz * 6.0));
        vec3 rice = mix(vec3(0.10, 0.24, 0.04), vec3(0.24, 0.34, 0.06), fbm(P.xz * 0.15));
        vec3 water = vec3(0.04, 0.07, 0.07);
        albedo = mix(water, rice, plants);
        specScale = mix(1.6, 0.2, plants);                              // the water between the rows glints
    }
    else if (uMatPattern == PAT_ROOFTILE) {
        float row = floor(P.y / 0.28);
        float col = floor((P.x + P.z + mod(row, 2.0) * 0.15) / 0.3);
        float f = fract(P.y / 0.28);
        albedo *= (0.75 + 0.35 * hash12(vec2(row, col))) * (0.7 + 0.3 * smoothstep(0.0, 0.8, f));
        specScale = 0.4;
    }
    else if (uMatPattern == PAT_GLASS) {
        if (abs(N.y) < 0.5) {
            vec2 uv = surfaceUV(P, N);
            vec2 cell = uv / vec2(1.6, 3.6);
            vec2 id = floor(cell);
            vec2 f = fract(cell);
            float mullion = max(step(f.x, 0.06), step(f.y, 0.1));
            vec3 glass = vec3(0.03, 0.06, 0.09) * (0.8 + 0.4 * hash12(id));
            float lit = step(0.62, hash12(id + floor(P.xz / 30.0) * 7.3));
            emissive += (1.0 - mullion) * vec3(0.7, 0.8, 1.0) * 0.3 * uNight * lit;
            albedo = mix(glass, albedo, mullion);
            specScale = mullion > 0.5 ? 0.8 : 5.0;
        }
    }
    else if (uMatPattern == PAT_CONCRETE) {
        vec2 uv = surfaceUV(P, N);
        albedo *= 0.78 + 0.4 * fbm(uv * 0.9);
        vec2 g = abs(fract(uv / 4.0 + 0.5) - 0.5) * 4.0;           // metres to nearest seam
        float seam = 1.0 - smoothstep(0.02, 0.05, min(g.x, g.y));
        albedo *= 1.0 - 0.5 * seam;
    }
    else if (uMatPattern == PAT_BRICK) {
        if (abs(N.y) > 0.5) {                                        // wall top: concrete coping
            albedo = vec3(0.30) * (0.8 + 0.3 * fbm(P.xz));
        } else {
            vec2 uv = surfaceUV(P, N);
            float row = floor(uv.y / 0.25);
            float bx = (uv.x + mod(row, 2.0) * 0.3) / 0.6;
            vec2 f = vec2(fract(bx), fract(uv.y / 0.25));
            vec2 id = vec2(floor(bx), row);
            float mortar = max(step(f.x, 0.04), step(f.y, 0.08));
            vec3 brick = albedo * (0.7 + 0.55 * hash12(id)) * (0.85 + 0.3 * vnoise(uv * 3.0));
            albedo = mix(brick, vec3(0.22, 0.21, 0.2), mortar);
            albedo *= mix(0.55, 1.0, smoothstep(0.0, 1.2, P.y));      // grime near the ground
            // fake relief: mortar grooves tilt the normal slightly
            N = normalize(N - 0.25 * mortar * vec3(0.0, sign(f.y - 0.5), 0.0));
        }
    }
    else if (uMatPattern == PAT_METAL) {
        if (abs(N.y) < 0.5) {
            bool alongX = abs(N.z) > abs(N.x);
            float u = alongX ? P.x : P.z;
            float ribs = sin(u * 6.2831 / 0.32);
            albedo *= 0.85 + 0.15 * ribs;
            N = normalize(N + (alongX ? vec3(1, 0, 0) : vec3(0, 0, 1)) * ribs * 0.35);
        }
        float rust = smoothstep(0.55, 0.8, fbm(vec2(P.x + P.z, P.y) * vec2(1.3, 2.5)));
        albedo = mix(albedo, vec3(0.16, 0.06, 0.02), rust * 0.7);
    }
    else if (uMatPattern == PAT_FACADE) {
        if (abs(N.y) < 0.5 && P.y > 1.2) {
            vec2 uv = surfaceUV(P, N);
            vec2 cell = uv / uFacadeCell;
            vec2 id = floor(cell);
            vec2 f = fract(cell);
            float win = step(0.2, f.x) * step(f.x, 0.8) * step(0.28, f.y) * step(f.y, 0.8);
            float sill = step(0.2, f.x) * step(f.x, 0.8) * step(0.22, f.y) * step(f.y, 0.28);
            if (win > 0.5) {
                albedo = vec3(0.012, 0.018, 0.03);
                specScale = 4.0;
                float lit = step(0.55, hash12(id + floor(P.xz / 40.0) * 13.1));
                vec3 warm = mix(vec3(1.0, 0.62, 0.28), vec3(0.75, 0.85, 1.0), step(0.8, hash12(id * 3.1)));
                emissive += warm * 0.3 * uNight * lit * (0.4 + 0.6 * hash12(id * 1.7));
            } else {
                albedo *= (0.85 + 0.25 * vnoise(uv * 2.0)) * (1.0 - 0.35 * sill);
                albedo *= mix(0.7, 1.0, smoothstep(0.0, 1.5, P.y));     // street grime
            }
        }
    }
    else if (uMatPattern == PAT_FOLIAGE) {
        albedo *= 0.55 + 0.8 * fbm(P.xz * 2.2 + P.y * 1.7);
        vec3 jitter = vec3(vnoise(P.xy * 3.0), vnoise(P.yz * 3.0), vnoise(P.zx * 3.0)) - 0.5;
        N = normalize(N + jitter * 0.9);
        specScale = 0.1;
    }
    else if (uMatPattern == PAT_BARK) {
        albedo *= 0.65 + 0.5 * vnoise(vec2((P.x + P.z) * 9.0, P.y * 1.3));
        specScale = 0.1;
    }
}

// 5x5 PCF shadow lookup with normal-offset + slope-scaled bias
float shadowFactor(vec3 P, vec3 N) {
    if (!uShadowsEnabled || !uSkyEnabled) return 1.0;
    vec3 L = normalize(-uSkyDir);
    vec4 ls = uLightSpace * vec4(P + N * 0.08, 1.0);
    vec3 c = ls.xyz / ls.w * 0.5 + 0.5;
    if (c.z > 1.0 || c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0) return 1.0;
    float bias = max(0.0012 * (1.0 - dot(N, L)), 0.0003);
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float lit = 0.0;
    for (int x = -2; x <= 2; ++x)
        for (int y = -2; y <= 2; ++y)
            lit += (c.z - bias > texture(uShadowMap, c.xy + vec2(x, y) * texel).r) ? 0.0 : 1.0;
    return lit / 25.0;
}

// reflections are a little weaker in shadow (the sky is partly blocked)
float shadowSoft(float s) { return 0.6 + 0.4 * s; }

// ACES filmic tone curve (Narkowicz fit)
vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 V = normalize(uViewPos - FragPos);
    vec3 N;
    if (uShadingModel == 2) {
        N = normalize(cross(dFdx(FragPos), dFdy(FragPos)));   // one normal per triangle
    } else {
        N = normalize(Normal);
    }

    vec3 albedo = pow(uMatDiffuse, vec3(2.2));
    vec3 emissive = pow(uMatEmissive, vec3(2.2)) * 3.0;
    float specScale = 1.0;
    applyPattern(FragPos, N, albedo, emissive, specScale);
    vec3 specColor = uMatSpecular * 0.5 * specScale;

    float shadow = shadowFactor(FragPos, N);
    vec3 color;
    if (uShadingModel == 1) {
        color = albedo * (gAmbientLocal + gSunDiffuse * shadow) + specColor * (gSpecLocal + gSunSpec * shadow);
    } else {
        vec3 sunDiff, sunSpec, locDiff, locSpec;
        skyLight(N, V, sunDiff, sunSpec);
        localLights(N, FragPos, V, locDiff, locSpec);
        color = albedo * (hemisphereAmbient(N) + locDiff + sunDiff * shadow) + specColor * (locSpec + sunSpec * shadow);
    }
    color += emissive;

    // Sky reflection (Schlick Fresnel): glass, water, windows and glossy paint mirror the sky gradient.
    //   F = F0 + (1 - F0) (1 - N.V)^5
    float reflectivity = 0.0;
    if (uMatPattern == PAT_WATER) reflectivity = 0.9;
    else if (uMatPattern == PAT_PADDY) reflectivity = 0.10 * (1.0 - smoothstep(0.05, 0.15, albedo.g));
    else if (uMatPattern == PAT_GLASS) reflectivity = specScale > 4.0 ? 0.75 : 0.15;
    else if (uMatPattern == PAT_FACADE && specScale > 3.0) reflectivity = 0.45;     // window panes
    else if (uMatPattern == PAT_NONE && uMatShininess >= 64.0) reflectivity = 0.22;  // car paint, polished metal
    if (reflectivity > 0.0 && uShadingModel != 2) {
        vec3 R = reflect(-V, N);
        vec3 sky = R.y > 0.0 ? mix(uFogColor, uSkyZenith, pow(R.y, 0.45))
                             : mix(uFogColor, uFogColor * 0.25, clamp(-R.y * 4.0, 0.0, 1.0));   // below the horizon: ground-ish
        float F = 0.04 + 0.96 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
        color += pow(sky, vec3(2.2)) / uExposure * mix(F, 1.0, 0.25) * reflectivity * shadowSoft(shadow);
    }

    // HDR -> display: exposure, filmic tone map, gamma
    color = aces(color * uExposure);
    color = pow(color, vec3(1.0 / 2.2));

    // Distance fog in display space so it matches the sky horizon exactly
    float d = length(FragPos - uViewPos);
    float fog = 1.0 - exp(-pow(d * uFogDensity, 2.0));
    color = mix(color, uFogColor, clamp(fog, 0.0, 1.0));

    // Alpha = glow mask for bloom: only light-emitting surfaces (lamps, windows,
    // headlights, signals, neon) glow, so bright daylight surfaces do not bloom.
    float glow = clamp(dot(emissive, vec3(0.299, 0.587, 0.114)) * uExposure * 1.3, 0.0, 1.0);
    FragColor = vec4(color, glow * (1.0 - clamp(fog, 0.0, 1.0)));
}
