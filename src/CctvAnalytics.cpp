#include "CctvAnalytics.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

bool CctvAnalytics::init() {
    if (!imageShader.load("shaders/image.vert", "shaders/image.frag")) return false;
    glGenTextures(3, thumbs);
    for (GLuint t : thumbs) {
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    return true;
}

void CctvAnalytics::reset() {
    background.clear();
    blobs.clear();
}

void CctvAnalytics::update(GLuint srcFbo, int texW, int texH, float dt, bool cameraMoved) {
    if (!motionEnabled) return;

    // 1. small grey frame (320 px wide): the GPU scales the frame down, the CPU reads only that
    int lw = std::min(320, texW), lh = std::max(1, lw * texH / std::max(1, texW));
    std::vector<uint8_t> rgba;
    reader.read(srcFbo, texW, texH, lw, lh, rgba);

    const size_t N = static_cast<size_t>(lw) * lh;
    if (lw != w || lh != h) { w = lw; h = lh; background.clear(); }
    grey.resize(N);
    for (int y = 0; y < h; ++y)                         // GL rows are bottom-up: flip so row 0 = top
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = &rgba[(static_cast<size_t>(h - 1 - y) * w + x) * 4];
            grey[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(imgops::luminance(p[0], p[1], p[2]));
        }

    // 2. background model (restart when the camera itself moves)
    if (background.size() != N || cameraMoved) {
        background.assign(grey.begin(), grey.end());
        warmup = 1.0f;
    }
    warmup = std::max(0.0f, warmup - dt);
    const float a = 1.0f - std::exp(-dt / tau);

    // 3. difference, threshold; selective update: foreground pixels adapt 10x slower
    diff.resize(N);
    mask = imgops::Mask(w, h);
    for (size_t i = 0; i < N; ++i) {
        float d = std::fabs(grey[i] - background[i]);
        diff[i] = static_cast<uint8_t>(std::min(255.0f, d));
        bool fg = warmup <= 0.0f && d > threshold;
        mask.m[i] = fg ? 1 : 0;
        // warm-up: converge quickly to the static scene; afterwards foreground adapts 10x slower
        float rate = warmup > 0.0f ? 0.3f : (fg ? 0.1f * a : a);
        background[i] += rate * (grey[i] - background[i]);
    }

    // 4. morphology + connected components
    imgops::Mask cleaned = imgops::dilate(imgops::dilate(imgops::erode(mask)));
    blobs = imgops::connectedComponents(cleaned, 12);
    mask = cleaned;
    uploadThumbs();
}

void CctvAnalytics::uploadThumbs() {
    const size_t N = static_cast<size_t>(w) * h;
    std::vector<uint8_t> rgb(N * 3);
    auto upload = [&](GLuint tex) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    };
    for (size_t i = 0; i < N; ++i) rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = static_cast<uint8_t>(background[i]);
    upload(thumbs[0]);
    for (size_t i = 0; i < N; ++i) rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = static_cast<uint8_t>(std::min(255, diff[i] * 4));
    upload(thumbs[1]);
    for (size_t i = 0; i < N; ++i) {
        rgb[i * 3] = mask.m[i] ? 255 : 20;
        rgb[i * 3 + 1] = mask.m[i] ? 60 : 20;
        rgb[i * 3 + 2] = mask.m[i] ? 40 : 28;
    }
    upload(thumbs[2]);
}

void CctvAnalytics::drawThumb(const GeometryManager& geo, GLuint tex, float x, float y, float tw, float th, int W, int H) {
    glDisable(GL_DEPTH_TEST);
    imageShader.use();
    imageShader.setVec4("uRect", glm::vec4(x, y, tw, th));
    imageShader.setVec2("uScreen", glm::vec2(static_cast<float>(W), static_cast<float>(H)));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    imageShader.setInt("uImage", 0);
    glUniform2i(imageShader.loc("uSize"), w, h);
    imageShader.setInt("uReveal", -1);
    imageShader.setFloat("uPixelOnScreen", 0.0f);
    geo.drawQuad();
    glEnable(GL_DEPTH_TEST);
}

void CctvAnalytics::render(Overlay2D& ui, const GeometryManager& geo, int W, int H,
                           const std::string& clock, bool showHud, float time) {
    const float u = H / 720.0f, ts = 1.5f * u;
    const glm::vec4 white(1.0f), green(0.3f, 1.0f, 0.45f, 1.0f), red(1.0f, 0.22f, 0.18f, 1.0f);

    if (showHud) {
        ui.begin(W, H);
        ui.rect(14 * u, 12 * u, 250 * u, 40 * u, glm::vec4(0, 0, 0, 0.45f));
        ui.text(22 * u, 18 * u, "CAM-01  JUNCTION (0,0)  NORTH-WEST", ts, white);
        ui.text(22 * u, 34 * u, "2026-10-05  " + clock, ts, white);
        if (std::fmod(time, 1.0f) < 0.6f) {
            ui.rect(W - 92 * u, 20 * u, 10 * u, 10 * u, red);
            ui.text(W - 76 * u, 20 * u, "REC", ts * 1.2f, red);
        }
        ui.end();
    }
    if (!motionEnabled) return;

    // boxes around moving objects
    ui.begin(W, H);
    float sx = static_cast<float>(W) / std::max(1, w), sy = static_cast<float>(H) / std::max(1, h);
    for (size_t i = 0; i < blobs.size(); ++i) {
        const imgops::Blob& b = blobs[i];
        float x0 = b.x0 * sx, y0 = b.y0 * sy, bw = (b.x1 - b.x0 + 1) * sx, bh = (b.y1 - b.y0 + 1) * sy;
        ui.outline(x0, y0, bw, bh, 2.0f * u, red);
        ui.rect(x0 - 2 * u, y0 - 13 * u, 30 * u, 11 * u, red);
        ui.text(x0, y0 - 11.5f * u, "#" + std::to_string(i + 1), ts * 0.85f, white);
    }
    char status[96];
    std::snprintf(status, sizeof(status), "MOTION: %d object%s   |  threshold %d  tau %.0fs",
                  objectCount(), objectCount() == 1 ? "" : "s", static_cast<int>(threshold), tau);
    ui.rect(14 * u, H - 34 * u, ui.textWidth(status, ts) + 16 * u, 22 * u, glm::vec4(0, 0, 0, 0.55f));
    ui.text(22 * u, H - 27 * u, warmup > 0.0f ? std::string("MOTION: learning background...") : std::string(status), ts,
            warmup > 0.0f ? white : green);

    // thumbnails of the pipeline steps (bottom right)
    float tw = W * 0.14f, th = tw * h / std::max(1, w), gap = 8 * u;
    float x = W - 3 * (tw + gap), y = H - th - 30 * u;
    const char* labels[3] = { "background B", "|I - B|  (x4)", "mask after opening" };
    for (int i = 0; i < 3; ++i) {
        ui.rect(x + i * (tw + gap) - 2, y - 16 * u, tw + 4, th + 18 * u, glm::vec4(0, 0, 0, 0.6f));
        ui.text(x + i * (tw + gap), y - 13 * u, labels[i], ts * 0.85f, white);
    }
    ui.end();
    for (int i = 0; i < 3; ++i) drawThumb(geo, thumbs[i], x + i * (tw + gap), y, tw, th, W, H);
}
