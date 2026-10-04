#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "Shader.h"

// Immediate-mode 2D drawing in screen pixels (origin top-left):
// filled rectangles, outlines and bitmap text. Everything queued between
// begin() and end() is uploaded and drawn in one call.
class Overlay2D {
public:
    Overlay2D();
    ~Overlay2D();

    bool init();
    void begin(int screenW, int screenH);
    void rect(float x, float y, float w, float h, const glm::vec4& color);
    void outline(float x, float y, float w, float h, float thickness, const glm::vec4& color);
    // Triangle with a colour per corner (gradients, light beams)
    void triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4& ca, const glm::vec4& cb, const glm::vec4& cc);
    void line(glm::vec2 a, glm::vec2 b, float thickness, const glm::vec4& color);
    // scale 1 = ~7px tall glyphs
    void text(float x, float y, const std::string& s, float scale, const glm::vec4& color);
    float textWidth(const std::string& s, float scale) const;
    // additive = true blends as light (src*alpha + dst): beams and glows
    void end(bool additive = false);

private:
    struct V { float x, y, r, g, b, a; };
    std::vector<V> verts;
    GLuint vao, vbo;
    Shader shader;
    int width, height;
};
