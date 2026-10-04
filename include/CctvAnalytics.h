#pragma once

#include <glad/glad.h>
#include <string>
#include <vector>
#include "ImageOps.h"
#include "Overlay2D.h"
#include "Shader.h"
#include "Geometry.h"
#include "Downsampler.h"

// ---------------------------------------------------------------------------
// CCTV analytics on the rendered camera frame (B key):
//
//   I  = grey frame, the camera image scaled to 320 px wide on the GPU and read back
//   B  = background model, running average  B += a (I - B),  a = 1 - e^(-dt / tau)
//   D  = |I - B|,   M = D > T
//   M' = dilate(dilate(erode(M)))        opening removes speckles, dilation merges parts
//   blobs = 8-connected components of M' with area >= minArea  -> bounding boxes
//
// Also draws the security-camera HUD (camera id, clock, REC, object count) and
// three thumbnails of the intermediate images.
// ---------------------------------------------------------------------------
class CctvAnalytics {
public:
    bool motionEnabled = false;
    float threshold = 18.0f;          // grey levels
    float tau = 3.0f;                 // background time constant (seconds)

    bool init();
    void reset();                     // background := current frame (camera moved)
    void update(GLuint srcFbo, int texW, int texH, float dt, bool cameraMoved);
    void render(Overlay2D& ui, const GeometryManager& geo, int W, int H,
                const std::string& clock, bool showHud, float time);
    int objectCount() const { return static_cast<int>(blobs.size()); }

private:
    int w = 0, h = 0;
    std::vector<float> background;
    std::vector<uint8_t> grey, diff;
    imgops::Mask mask;
    std::vector<imgops::Blob> blobs;
    float warmup = 0.0f;
    GLuint thumbs[3] = { 0, 0, 0 };   // background, difference, mask
    Shader imageShader;
    Downsampler reader;

    void uploadThumbs();
    void drawThumb(const GeometryManager& geo, GLuint tex, float x, float y, float tw, float th, int W, int H);
};
