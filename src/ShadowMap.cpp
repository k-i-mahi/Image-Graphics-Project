#include "ShadowMap.h"
#include <iostream>

ShadowMap::ShadowMap() : fbo(0), depthTexture(0), size(0) {}

ShadowMap::~ShadowMap() {
    cleanup();
}

bool ShadowMap::init(int resolution) {
    cleanup();
    size = resolution;

    glGenTextures(1, &depthTexture);
    glBindTexture(GL_TEXTURE_2D, depthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[] = { 1.0f, 1.0f, 1.0f, 1.0f };   // outside the map = fully lit
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!ok) std::cerr << "[ERROR] Shadow map framebuffer incomplete." << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}

void ShadowMap::begin() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, size, size);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_CULL_FACE);                 // thin/open meshes must still cast
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 3.0f);             // slope-scaled depth bias against acne
}

void ShadowMap::end() const {
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ShadowMap::cleanup() {
    if (depthTexture) { glDeleteTextures(1, &depthTexture); depthTexture = 0; }
    if (fbo) { glDeleteFramebuffers(1, &fbo); fbo = 0; }
}
