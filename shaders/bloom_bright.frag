#version 330 core
// Bloom pass 1: half-resolution bright pass. The scene's alpha channel is the
// glow mask written by scene.frag / sky.frag, so only emitters contribute.
in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D uScene;
uniform vec2 uTexel;   // 1 / full-resolution size

void main() {
    // 2x2 box downsample (4 taps around the half-res pixel centre)
    vec4 c = texture(uScene, TexCoords + uTexel * vec2(-0.5, -0.5))
           + texture(uScene, TexCoords + uTexel * vec2( 0.5, -0.5))
           + texture(uScene, TexCoords + uTexel * vec2(-0.5,  0.5))
           + texture(uScene, TexCoords + uTexel * vec2( 0.5,  0.5));
    c *= 0.25;
    FragColor = vec4(c.rgb * c.a, 1.0);
}
