#version 330 core
layout (location = 0) in vec3 aPos;   // full-screen quad in NDC

uniform mat4 uInvViewProjRot;          // inverse(projection * rotation-only view)

out vec3 vRay;

void main() {
    vec4 p = uInvViewProjRot * vec4(aPos.xy, 1.0, 1.0);
    vRay = p.xyz / p.w;
    gl_Position = vec4(aPos.xy, 1.0, 1.0);
}
