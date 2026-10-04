#pragma once

#include <glad/glad.h>
#include <iostream>

// Offscreen "sensor" framebuffer.
// The scene is drawn into a 4x multisampled framebuffer (anti-aliasing), then
// resolve() copies it into plain color + depth textures that the DIP stage reads.
class FramebufferObject {
public:
    GLuint fboID;          // resolved (single-sample) framebuffer
    GLuint colorTexture;
    GLuint depthTexture;
    GLuint msFBO;          // multisampled framebuffer the scene renders into
    GLuint msColor;
    GLuint msDepth;
    int samples;
    int width;
    int height;

    FramebufferObject();
    ~FramebufferObject();

    bool init(int w, int h);
    void bind() const;      // binds the multisampled target
    void resolve() const;   // MSAA -> color/depth textures
    void unbind() const;
    void resize(int w, int h);
    void cleanup();

    GLuint getColorTexture() const { return colorTexture; }
    GLuint getDepthTexture() const { return depthTexture; }
};
