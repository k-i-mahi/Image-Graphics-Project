#version 330 core
// Separable Gaussian blur: a 2D Gaussian G(x,y) = g(x) g(y), so one 9-tap
// horizontal pass followed by one vertical pass costs 18 samples instead of 81.
// Weights are the binomial row 1 8 28 56 70 56 28 8 1 / 256.
in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D uImage;
uniform vec2 uStep;    // one texel along x (horizontal pass) or y (vertical pass), times the spread

const float W[5] = float[](70.0, 56.0, 28.0, 8.0, 1.0);

void main() {
    vec3 sum = texture(uImage, TexCoords).rgb * W[0];
    for (int i = 1; i < 5; ++i) {
        sum += texture(uImage, TexCoords + uStep * float(i)).rgb * W[i];
        sum += texture(uImage, TexCoords - uStep * float(i)).rgb * W[i];
    }
    FragColor = vec4(sum / 256.0, 1.0);
}
