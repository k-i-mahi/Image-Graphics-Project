#pragma once

#include <glad/glad.h>
#include <vector>

// Reads a small copy of a framebuffer back to the CPU: the GPU scales the image
// down with a linear-filtered blit, then only the small image is transferred.
// Used by the live histogram (DIPProcessor) and the motion detector (CctvAnalytics).
class Downsampler {
public:
    // Copies srcFbo (srcW x srcH) into an RGBA image of dstW x dstH, row 0 = BOTTOM (OpenGL order)
    void read(GLuint srcFbo, int srcW, int srcH, int dstW, int dstH, std::vector<unsigned char>& rgba) {
        if (fbo == 0) {
            glGenFramebuffers(1, &fbo);
            glGenRenderbuffers(1, &rbo);
        }
        if (dstW != w || dstH != h) {
            w = dstW;
            h = dstH;
            glBindRenderbuffer(GL_RENDERBUFFER, rbo);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
        glBlitFramebuffer(0, 0, srcW, srcH, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        rgba.resize(static_cast<size_t>(w) * h * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

private:
    GLuint fbo = 0, rbo = 0;
    int w = 0, h = 0;
};
