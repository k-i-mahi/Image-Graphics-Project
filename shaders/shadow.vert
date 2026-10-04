#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 uModel;
uniform mat4 uLightSpace;   // light projection * light view

void main() {
    gl_Position = uLightSpace * uModel * vec4(aPos, 1.0);
}
