#include "Overlay2D.h"
#include "stb_easy_font.h"

Overlay2D::Overlay2D() : vao(0), vbo(0), width(1), height(1) {}

Overlay2D::~Overlay2D() {}

bool Overlay2D::init() {
    if (!shader.load("shaders/overlay.vert", "shaders/overlay.frag")) return false;

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(V), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
    return true;
}

void Overlay2D::begin(int screenW, int screenH) {
    width = screenW;
    height = screenH;
    verts.clear();
}

void Overlay2D::rect(float x, float y, float w, float h, const glm::vec4& c) {
    V a{ x,     y,     c.r, c.g, c.b, c.a };
    V b{ x + w, y,     c.r, c.g, c.b, c.a };
    V d{ x + w, y + h, c.r, c.g, c.b, c.a };
    V e{ x,     y + h, c.r, c.g, c.b, c.a };
    verts.insert(verts.end(), { a, b, d, a, d, e });
}

void Overlay2D::outline(float x, float y, float w, float h, float t, const glm::vec4& c) {
    rect(x - t, y - t, w + 2 * t, t, c);     // top
    rect(x - t, y + h, w + 2 * t, t, c);     // bottom
    rect(x - t, y, t, h, c);                 // left
    rect(x + w, y, t, h, c);                 // right
}

void Overlay2D::triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4& ca, const glm::vec4& cb, const glm::vec4& cc) {
    verts.push_back({ a.x, a.y, ca.r, ca.g, ca.b, ca.a });
    verts.push_back({ b.x, b.y, cb.r, cb.g, cb.b, cb.a });
    verts.push_back({ c.x, c.y, cc.r, cc.g, cc.b, cc.a });
}

void Overlay2D::line(glm::vec2 a, glm::vec2 b, float t, const glm::vec4& c) {
    glm::vec2 d = b - a;
    float len = glm::length(d);
    if (len < 1e-4f) return;
    glm::vec2 n = glm::vec2(-d.y, d.x) / len * (t * 0.5f);
    triangle(a - n, a + n, b + n, c, c, c);
    triangle(a - n, b + n, b - n, c, c, c);
}

void Overlay2D::text(float x, float y, const std::string& s, float scale, const glm::vec4& c) {
    // stb_easy_font emits quads: 4 vertices of {x, y, z, rgba8} = 16 bytes each
    static char buffer[99999];
    unsigned char col[4] = { 255, 255, 255, 255 };
    int quads = stb_easy_font_print(0, 0, const_cast<char*>(s.c_str()), col, buffer, sizeof(buffer));
    for (int q = 0; q < quads; ++q) {
        float* p = reinterpret_cast<float*>(buffer + q * 64);
        float x0 = x + p[0] * scale, y0 = y + p[1] * scale;
        float x1 = x + p[8] * scale, y1 = y + p[9] * scale; // opposite corner (vertex 2)
        rect(x0, y0, x1 - x0, y1 - y0, c);
    }
}

float Overlay2D::textWidth(const std::string& s, float scale) const {
    return stb_easy_font_width(const_cast<char*>(s.c_str())) * scale;
}

void Overlay2D::end(bool additive) {
    if (verts.empty()) return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);

    shader.use();
    shader.setVec2("uScreen", static_cast<float>(width), static_cast<float>(height));
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(V), verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}
