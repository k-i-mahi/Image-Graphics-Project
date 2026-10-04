#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include "Shader.h"

// Immediate-mode 2D drawing in screen pixels (origin top-left): rectangles,
// rounded panels with soft shadows, gradients, lines and anti-aliased text.
// Everything queued between begin() and end() is drawn in one call.
//
// Text uses TrueType fonts from Windows (Segoe UI, Consolas) rasterised once
// into a font atlas with stb_truetype; if they are missing it falls back to
// the built-in stb_easy_font bitmap font. `scale` 1 = glyphs ~7 px tall.
class Overlay2D {
public:
    enum Font { UI = 0, BOLD = 1, MONO = 2, FONT_COUNT };

    Overlay2D();
    ~Overlay2D();

    bool init();
    void begin(int screenW, int screenH);
    void rect(float x, float y, float w, float h, const glm::vec4& color);
    void outline(float x, float y, float w, float h, float thickness, const glm::vec4& color);
    void gradient(float x, float y, float w, float h, const glm::vec4& top, const glm::vec4& bottom);
    void roundRect(float x, float y, float w, float h, float radius, const glm::vec4& color);
    void roundOutline(float x, float y, float w, float h, float radius, float thickness, const glm::vec4& color);
    // Rounded card with a soft drop shadow
    void panel(float x, float y, float w, float h, float radius, const glm::vec4& fill, float shadow = 1.0f);
    // Triangle with a colour per corner (gradients, light beams)
    void triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4& ca, const glm::vec4& cb, const glm::vec4& cc);
    void line(glm::vec2 a, glm::vec2 b, float thickness, const glm::vec4& color);
    void text(float x, float y, const std::string& s, float scale, const glm::vec4& color, Font font = UI);
    float textWidth(const std::string& s, float scale, Font font = UI) const;
    // additive = true blends as light (src*alpha + dst): beams and glows
    void end(bool additive = false);

private:
    struct V { float x, y, u, v, r, g, b, a; };
    struct Glyph { float u0, v0, u1, v1, x0, y0, x1, y1, advance; };   // atlas uv + offsets at bake size
    std::vector<V> verts;
    GLuint vao, vbo, atlas;
    Shader shader;
    int width, height;
    bool ttf;
    std::unordered_map<int, Glyph> glyphs[FONT_COUNT];
    float ascent[FONT_COUNT];

    bool loadFonts();
    const Glyph* glyph(Font f, int codepoint) const;
    void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, const glm::vec4& c);
    void corner(float cx, float cy, float r, float a0, const glm::vec4& c);
};
