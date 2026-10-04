#version 330 core
// Shows an image pixel-exact (texelFetch, row 0 = top). Pixels after uReveal in
// raster order are drawn as "not processed yet" so the output can be watched
// being built one pixel at a time.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uImage;
uniform ivec2 uSize;          // image width, height in pixels
uniform int uReveal;          // pixels [0, uReveal) are shown; -1 = all
uniform float uPixelOnScreen; // size of one image pixel in screen pixels (for grid lines)

void main() {
    ivec2 p = clamp(ivec2(vUV * vec2(uSize)), ivec2(0), uSize - 1);
    vec3 c = texelFetch(uImage, p, 0).rgb;
    int idx = p.y * uSize.x + p.x;
    if (uReveal >= 0 && idx >= uReveal) {
        // pending: dark, desaturated, diagonal hatching
        float hatch = step(0.5, fract((gl_FragCoord.x + gl_FragCoord.y) / 10.0));
        c = vec3(dot(c, vec3(0.299, 0.587, 0.114))) * 0.18 + vec3(0.05, 0.06, 0.09) + hatch * 0.025;
    }
    // pixel grid when pixels are big enough to see
    if (uPixelOnScreen >= 7.0) {
        vec2 f = fract(vUV * vec2(uSize));
        vec2 edge = f * uPixelOnScreen;
        if (min(edge.x, edge.y) < 1.0) c *= 0.55;
    }
    FragColor = vec4(c, 1.0);
}
