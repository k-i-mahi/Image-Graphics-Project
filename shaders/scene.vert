#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;

// Gouraud: lighting evaluated per vertex, interpolated across the triangle.
// Kept as separate terms so the fragment shader can still apply the
// shadow (sun only) and the per-pixel surface pattern (albedo).
out vec3 gAmbientLocal;  // hemisphere + lamps/spots diffuse
out vec3 gSunDiffuse;
out vec3 gSpecLocal;
out vec3 gSunSpec;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform int uShadingModel; // 0 = Phong, 1 = Gouraud, 2 = Flat
uniform vec3 uViewPos;

#include "lighting.glsl"

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal = normalize(transpose(inverse(mat3(uModel))) * aNormal); // correct under non-uniform scale
    TexCoords = aTexCoords;
    gl_Position = uProjection * uView * worldPos;

    gAmbientLocal = vec3(0.0);
    gSunDiffuse = vec3(0.0);
    gSpecLocal = vec3(0.0);
    gSunSpec = vec3(0.0);
    if (uShadingModel == 1) {
        vec3 V = normalize(uViewPos - FragPos);
        vec3 localDiff;
        skyLight(Normal, V, gSunDiffuse, gSunSpec);
        localLights(Normal, FragPos, V, localDiff, gSpecLocal);
        gAmbientLocal = hemisphereAmbient(Normal) + localDiff;
    }
}
