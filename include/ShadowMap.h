#pragma once

#include <glad/glad.h>

// Depth-only framebuffer rendered from the sun/moon. The scene shader
// compares each fragment's light-space depth against it (PCF) to find shadows.
class ShadowMap {
public:
    GLuint fbo;
    GLuint depthTexture;
    int size;

    ShadowMap();
    ~ShadowMap();

    bool init(int resolution);
    void begin() const;   // bind + clear, depth bias on
    void end() const;     // restore state, bind default framebuffer
    void cleanup();
};
