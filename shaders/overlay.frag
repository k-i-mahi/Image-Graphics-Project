#version 330 core
in vec4 vColor;
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uFont;   // R8 coverage atlas

void main() {
    if (vUV.x < 0.0) {
        FragColor = vColor;                                         // shapes
    } else {
        float coverage = texture(uFont, vUV).r;                     // text: anti-aliased glyph coverage
        FragColor = vec4(vColor.rgb, vColor.a * coverage);
    }
}
