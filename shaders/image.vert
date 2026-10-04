#version 330 core
// Draws an image into a screen rectangle given in pixels (origin top-left).
layout (location = 0) in vec3 aPos;
layout (location = 2) in vec2 aTexCoords;

uniform vec4 uRect;     // x, y, width, height in screen pixels
uniform vec2 uScreen;

out vec2 vUV;           // 0..1, v = 0 at the TOP of the image

void main() {
    vUV = vec2(aTexCoords.x, 1.0 - aTexCoords.y);
    vec2 px = uRect.xy + vUV * uRect.zw;
    gl_Position = vec4(px.x / uScreen.x * 2.0 - 1.0, 1.0 - px.y / uScreen.y * 2.0, 0.0, 1.0);
}
