#include "Overlay2D.h"
#include "stb_easy_font.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <cmath>
#include <fstream>
#include <iterator>

namespace {
const float BAKE_PX = 36.0f;          // glyph size in the atlas
const int ATLAS = 2048;
const float EM_PER_SCALE = 10.0f;     // scale 1 -> 10 px em (~7 px capitals, like the bitmap font)

// UTF-8 -> code points (enough for the symbols used on screen)
std::vector<int> decode(const std::string& s) {
    std::vector<int> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        int cp = c, extra = 0;
        if (c >= 0xF0) { cp = c & 0x07; extra = 3; }
        else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; }
        else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }
        ++i;
        for (int k = 0; k < extra && i < s.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
        out.push_back(cp);
    }
    return out;
}
}

Overlay2D::Overlay2D() : vao(0), vbo(0), atlas(0), width(1), height(1), ttf(false), ascent{ 0, 0, 0 } {}

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
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(V), (void*)(4 * sizeof(float)));
    glBindVertexArray(0);
    ttf = loadFonts();
    return true;
}

// Rasterises three Windows fonts into one atlas: printable ASCII, Latin-1 (x, /, degree, squared, middle dot),
// Greek sigma letters and arrows.
bool Overlay2D::loadFonts() {
    const char* files[FONT_COUNT] = { "C:/Windows/Fonts/seguisb.ttf", "C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/consola.ttf" };
    const char* fallback[FONT_COUNT] = { "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arialbd.ttf", "C:/Windows/Fonts/cour.ttf" };
    std::vector<std::vector<unsigned char>> data(FONT_COUNT);
    for (int f = 0; f < FONT_COUNT; ++f) {
        for (const char* path : { files[f], fallback[f] }) {
            std::ifstream in(path, std::ios::binary);
            if (in) { data[f].assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()); break; }
        }
        if (data[f].empty()) return false;
    }

    std::vector<unsigned char> pixels(static_cast<size_t>(ATLAS) * ATLAS, 0);
    stbtt_pack_context pc;
    if (!stbtt_PackBegin(&pc, pixels.data(), ATLAS, ATLAS, 0, 4, nullptr)) return false;
    stbtt_PackSetOversampling(&pc, 2, 2);
    const int ranges[][2] = { { 32, 95 }, { 0xA0, 96 }, { 0x391, 57 }, { 0x2013, 40 }, { 0x2190, 12 }, { 0x21BB, 1 },
                              { 0x2211, 14 }, { 0x2264, 2 }, { 0x25A0, 96 } };
    const int nRanges = sizeof(ranges) / sizeof(ranges[0]);
    bool ok = true;
    for (int f = 0; f < FONT_COUNT && ok; ++f) {
        stbtt_fontinfo info;
        stbtt_InitFont(&info, data[f].data(), stbtt_GetFontOffsetForIndex(data[f].data(), 0));
        int asc, desc, gap;
        stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
        ascent[f] = asc * stbtt_ScaleForPixelHeight(&info, BAKE_PX);
        for (int r = 0; r < nRanges; ++r) {
            std::vector<stbtt_packedchar> chars(ranges[r][1]);
            stbtt_pack_range range{};
            range.font_size = BAKE_PX;
            range.first_unicode_codepoint_in_range = ranges[r][0];
            range.num_chars = ranges[r][1];
            range.chardata_for_range = chars.data();
            if (!stbtt_PackFontRanges(&pc, data[f].data(), 0, &range, 1)) { ok = r > 0; break; }
            for (int i = 0; i < ranges[r][1]; ++i) {
                const stbtt_packedchar& p = chars[i];
                if (p.x1 == p.x0 && ranges[r][0] + i != 32) continue;          // glyph not in the font
                glyphs[f][ranges[r][0] + i] = { p.x0 / float(ATLAS), p.y0 / float(ATLAS), p.x1 / float(ATLAS), p.y1 / float(ATLAS),
                                                p.xoff, p.yoff, p.xoff2, p.yoff2, p.xadvance };
            }
        }
    }
    stbtt_PackEnd(&pc);
    if (!ok) return false;

    glGenTextures(1, &atlas);
    glBindTexture(GL_TEXTURE_2D, atlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ATLAS, ATLAS, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glGenerateMipmap(GL_TEXTURE_2D);   // small text samples a smaller mip level: smooth, no shimmer
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

const Overlay2D::Glyph* Overlay2D::glyph(Font f, int cp) const {
    auto it = glyphs[f].find(cp);
    if (it != glyphs[f].end()) return &it->second;
    it = glyphs[f].find('?');
    return it != glyphs[f].end() ? &it->second : nullptr;
}

void Overlay2D::begin(int screenW, int screenH) {
    width = screenW;
    height = screenH;
    verts.clear();
}

void Overlay2D::quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, const glm::vec4& c) {
    V a{ x0, y0, u0, v0, c.r, c.g, c.b, c.a };
    V b{ x1, y0, u1, v0, c.r, c.g, c.b, c.a };
    V d{ x1, y1, u1, v1, c.r, c.g, c.b, c.a };
    V e{ x0, y1, u0, v1, c.r, c.g, c.b, c.a };
    verts.insert(verts.end(), { a, b, d, a, d, e });
}

void Overlay2D::rect(float x, float y, float w, float h, const glm::vec4& c) {
    quad(x, y, x + w, y + h, -1, -1, -1, -1, c);   // u < 0: solid colour
}

void Overlay2D::outline(float x, float y, float w, float h, float t, const glm::vec4& c) {
    rect(x - t, y - t, w + 2 * t, t, c);     // top
    rect(x - t, y + h, w + 2 * t, t, c);     // bottom
    rect(x - t, y, t, h, c);                 // left
    rect(x + w, y, t, h, c);                 // right
}

void Overlay2D::gradient(float x, float y, float w, float h, const glm::vec4& top, const glm::vec4& bottom) {
    triangle({ x, y }, { x + w, y }, { x + w, y + h }, top, top, bottom);
    triangle({ x, y }, { x + w, y + h }, { x, y + h }, top, bottom, bottom);
}

void Overlay2D::corner(float cx, float cy, float r, float a0, const glm::vec4& c) {
    const int seg = 6;
    for (int i = 0; i < seg; ++i) {
        float t0 = a0 + 1.5707963f * i / seg, t1 = a0 + 1.5707963f * (i + 1) / seg;
        triangle({ cx, cy }, { cx + r * std::cos(t0), cy + r * std::sin(t0) }, { cx + r * std::cos(t1), cy + r * std::sin(t1) }, c, c, c);
    }
}

void Overlay2D::roundRect(float x, float y, float w, float h, float r, const glm::vec4& c) {
    r = std::fmin(r, std::fmin(w, h) * 0.5f);
    if (r < 0.5f) { rect(x, y, w, h, c); return; }
    rect(x + r, y, w - 2 * r, h, c);
    rect(x, y + r, r, h - 2 * r, c);
    rect(x + w - r, y + r, r, h - 2 * r, c);
    corner(x + r, y + r, r, 3.14159265f, c);
    corner(x + w - r, y + r, r, 4.71238898f, c);
    corner(x + w - r, y + h - r, r, 0.0f, c);
    corner(x + r, y + h - r, r, 1.5707963f, c);
}

void Overlay2D::roundOutline(float x, float y, float w, float h, float r, float t, const glm::vec4& c) {
    r = std::fmin(r, std::fmin(w, h) * 0.5f);
    rect(x + r, y - t, w - 2 * r, t, c);
    rect(x + r, y + h, w - 2 * r, t, c);
    rect(x - t, y + r, t, h - 2 * r, c);
    rect(x + w, y + r, t, h - 2 * r, c);
    const int seg = 6;
    const float cx[4] = { x + r, x + w - r, x + w - r, x + r }, cy[4] = { y + r, y + r, y + h - r, y + h - r };
    const float a0[4] = { 3.14159265f, 4.71238898f, 0.0f, 1.5707963f };
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i < seg; ++i) {
            float t0 = a0[k] + 1.5707963f * i / seg, t1 = a0[k] + 1.5707963f * (i + 1) / seg;
            glm::vec2 p0(cx[k] + r * std::cos(t0), cy[k] + r * std::sin(t0)), p1(cx[k] + r * std::cos(t1), cy[k] + r * std::sin(t1));
            glm::vec2 q0(cx[k] + (r + t) * std::cos(t0), cy[k] + (r + t) * std::sin(t0)), q1(cx[k] + (r + t) * std::cos(t1), cy[k] + (r + t) * std::sin(t1));
            triangle(p0, q0, q1, c, c, c);
            triangle(p0, q1, p1, c, c, c);
        }
}

void Overlay2D::panel(float x, float y, float w, float h, float r, const glm::vec4& fill, float shadow) {
    for (int i = 6; i >= 1 && shadow > 0.0f; --i) {        // soft shadow: stacked, growing, faint rounded rects
        float g = i * 2.0f;
        roundRect(x - g * 0.5f, y - g * 0.5f + 3.0f, w + g, h + g, r + g * 0.5f, glm::vec4(0, 0, 0, 0.045f * shadow));
    }
    roundRect(x, y, w, h, r, fill);
}

void Overlay2D::triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4& ca, const glm::vec4& cb, const glm::vec4& cc) {
    verts.push_back({ a.x, a.y, -1, -1, ca.r, ca.g, ca.b, ca.a });
    verts.push_back({ b.x, b.y, -1, -1, cb.r, cb.g, cb.b, cb.a });
    verts.push_back({ c.x, c.y, -1, -1, cc.r, cc.g, cc.b, cc.a });
}

void Overlay2D::line(glm::vec2 a, glm::vec2 b, float t, const glm::vec4& c) {
    glm::vec2 d = b - a;
    float len = glm::length(d);
    if (len < 1e-4f) return;
    glm::vec2 n = glm::vec2(-d.y, d.x) / len * (t * 0.5f);
    triangle(a - n, a + n, b + n, c, c, c);
    triangle(a - n, b + n, b - n, c, c, c);
}

void Overlay2D::text(float x, float y, const std::string& s, float scale, const glm::vec4& c, Font font) {
    if (!ttf) {
        // stb_easy_font emits quads: 4 vertices of {x, y, z, rgba8} = 16 bytes each
        static char buffer[99999];
        unsigned char col[4] = { 255, 255, 255, 255 };
        int quads = stb_easy_font_print(0, 0, const_cast<char*>(s.c_str()), col, buffer, sizeof(buffer));
        for (int q = 0; q < quads; ++q) {
            float* p = reinterpret_cast<float*>(buffer + q * 64);
            rect(x + p[0] * scale, y + p[1] * scale, (p[8] - p[0]) * scale, (p[9] - p[1]) * scale, c);
        }
        return;
    }
    const float k = scale * EM_PER_SCALE / BAKE_PX;          // atlas pixels -> screen pixels
    float pen = x, baseline = y + 7.3f * scale;               // capitals span y .. y + 7 scale
    for (int cp : decode(s)) {
        const Glyph* g = glyph(font, cp);
        if (!g) continue;
        if (g->x1 > g->x0) quad(pen + g->x0 * k, baseline + g->y0 * k, pen + g->x1 * k, baseline + g->y1 * k, g->u0, g->v0, g->u1, g->v1, c);
        pen += g->advance * k;
    }
}

float Overlay2D::textWidth(const std::string& s, float scale, Font font) const {
    if (!ttf) return stb_easy_font_width(const_cast<char*>(s.c_str())) * scale;
    const float k = scale * EM_PER_SCALE / BAKE_PX;
    float w = 0.0f;
    for (int cp : decode(s)) {
        const Glyph* g = glyph(font, cp);
        if (g) w += g->advance * k;
    }
    return w;
}

void Overlay2D::end(bool additive) {
    if (verts.empty()) return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);

    shader.use();
    shader.setVec2("uScreen", static_cast<float>(width), static_cast<float>(height));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas);
    shader.setInt("uFont", 0);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(V), verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}
