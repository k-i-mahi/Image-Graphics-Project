#version 330 core
in vec3 vRay;
out vec4 FragColor;

// All colours in display space (sky is not tone mapped)
uniform vec3 uZenith;
uniform vec3 uHorizon;
uniform vec3 uToSun;
uniform vec3 uSunColor;
uniform float uSunVisible;   // 0..1, fades as the sun sets
uniform vec3 uToMoon;
uniform float uDay;          // 0 = night, 1 = day
uniform float uTime;

#include "common.glsl"

void main() {
    vec3 d = normalize(vRay);
    float h = d.y;

    // Vertical gradient: horizon colour -> zenith colour
    vec3 col = mix(uHorizon, uZenith, pow(clamp(h, 0.0, 1.0), 0.45));
    if (h < 0.0) col = uHorizon * mix(1.0, 0.55, clamp(-h * 4.0, 0.0, 1.0));

    // Sun glow + disk
    float sd = max(dot(d, uToSun), 0.0);
    col += uSunColor * (pow(sd, 8.0) * 0.22 + pow(sd, 90.0) * 0.45) * uSunVisible;
    float disk = smoothstep(0.99955, 0.99972, sd) * uSunVisible;
    col = mix(col, mix(uSunColor, vec3(1.0), 0.6) * 1.15, disk);

    float night = 1.0 - uDay;

    // Stars (hashed cells on the sky sphere, twinkling)
    if (night > 0.01 && h > 0.0) {
        vec3 sp = d * 260.0;
        vec3 id = floor(sp);
        float r = hash13(id);
        if (r > 0.9965) {
            float s = smoothstep(0.35, 0.0, length(fract(sp) - 0.5));
            float twinkle = 0.6 + 0.4 * sin(uTime * 3.0 + r * 200.0);
            col += vec3(0.9, 0.95, 1.0) * s * twinkle * night * smoothstep(0.0, 0.2, h);
        }
    }

    // Moon disk + halo
    float md = max(dot(d, uToMoon), 0.0);
    col = mix(col, vec3(0.86, 0.89, 0.95), smoothstep(0.99935, 0.9996, md) * night);
    col += vec3(0.35, 0.45, 0.65) * pow(md, 300.0) * 0.4 * night;

    // Clouds: fbm projected onto a plane above the camera, drifting with time
    if (h > 0.01) {
        vec2 uv = d.xz / (h + 0.1) * 0.9 + vec2(uTime * 0.012, uTime * 0.005);
        float c = smoothstep(0.48, 0.82, fbm(uv * 1.1));
        c *= smoothstep(0.01, 0.3, h);
        vec3 cloud = mix(vec3(0.06, 0.07, 0.1), mix(uSunColor, vec3(1.0), 0.65) * 0.95, uDay);
        cloud += uSunColor * pow(sd, 5.0) * 0.35 * uSunVisible;   // bright edge towards the sun
        col = mix(col, cloud, c * 0.85);
    }

    // alpha = glow mask for bloom: only the sun disc and the moon
    FragColor = vec4(col, smoothstep(0.93, 1.0, max(col.r, max(col.g, col.b))));
}
