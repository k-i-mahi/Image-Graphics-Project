#include "Bloom.h"
#include <algorithm>

bool Bloom::init(int width, int height) {
    if (!bright.load("shaders/quad.vert", "shaders/bloom_bright.frag")) return false;
    if (!blur.load("shaders/quad.vert", "shaders/bloom_blur.frag")) return false;
    glGenFramebuffers(2, fbo);
    glGenTextures(2, tex);
    fullW = width;
    fullH = height;
    allocate();
    return true;
}

void Bloom::resize(int width, int height) {
    if (width == fullW && height == fullH) return;
    fullW = width;
    fullH = height;
    allocate();
}

void Bloom::allocate() {
    w = std::max(1, fullW / 2);
    h = std::max(1, fullH / 2);
    for (int i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

GLuint Bloom::run(const GeometryManager& geo, GLuint sceneColor) {
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, w, h);

    // 1. bright pass: full-res scene -> half-res glow
    glBindFramebuffer(GL_FRAMEBUFFER, fbo[0]);
    bright.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneColor);
    bright.setInt("uScene", 0);
    bright.setVec2("uTexel", glm::vec2(1.0f / fullW, 1.0f / fullH));
    geo.drawQuad();

    // 2. separable Gaussian, three rounds with a growing spread -> wide, soft halo
    blur.use();
    blur.setInt("uImage", 0);
    const float spread[3] = { 1.0f, 2.0f, 3.5f };
    for (float sp : spread) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[1]);
        glBindTexture(GL_TEXTURE_2D, tex[0]);
        blur.setVec2("uStep", glm::vec2(sp / w, 0.0f));
        geo.drawQuad();
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[0]);
        glBindTexture(GL_TEXTURE_2D, tex[1]);
        blur.setVec2("uStep", glm::vec2(0.0f, sp / h));
        geo.drawQuad();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);
    return tex[0];
}
