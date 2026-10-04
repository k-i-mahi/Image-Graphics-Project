#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "Overlay2D.h"
#include "Shader.h"
#include "Geometry.h"

// ---------------------------------------------------------------------------
// Image Operation Lab (view 0)
//
//   ORIGINAL image  |  OPERATION (light source + editable kernel)  |  PROCESSED image
//   input zoom      |  the arithmetic for the current pixel         |  output zoom
//
// A snapshot of the 3D view is reduced to a small image (e.g. 160 x 90) so that
// single pixels are visible. The operation is applied on the CPU exactly as
// written in the formulas on screen; the processed image is revealed pixel by
// pixel in raster order while a light beam points at the kernel window in the
// original and at the output pixel in the processed image.
// ---------------------------------------------------------------------------

enum LabOp { OP_CONVOLVE = 0, OP_MEDIAN, OP_MIN, OP_MAX, OP_HISTEQ, OP_COUNT };
enum LabNoise { NOISE_NONE = 0, NOISE_GAUSSIAN, NOISE_SALT_PEPPER, NOISE_COUNT };

struct LabKernel {
    int size = 3;
    float w[25] = { 0 };
    float divisor = 1.0f;
    bool autoDiv = true;      // divisor = sum of weights (1 if the sum is 0)
    bool absolute = false;    // |result| (edge detectors give signed values)
    bool offset128 = false;   // + 128 (emboss: shows negative values as dark)
};

class FilterLab {
public:
    bool active = false;
    bool captureRequested = false;

    bool init();
    void open();                                   // enter the lab; asks for a snapshot
    void capture(GLuint colorTex, int texW, int texH);
    void update(float dt);
    void render(Overlay2D& ui, const GeometryManager& geo, int W, int H, float time);

    // Input forwarded from GLFW callbacks (screen pixels, origin top-left)
    void onMouseMove(float x, float y) { mouse = glm::vec2(x, y); }
    void onMouseButton(bool down) { if (!down) clickPending = true; }
    void onScroll(float dy) { scrollPending += dy; }
    void onChar(unsigned int c);
    void onKey(int key);                           // press / repeat
    std::string statusLine() const;

private:
    // ---- images (RGB, row 0 = top) ----
    std::vector<unsigned char> snapshot;           // full-resolution capture (RGBA)
    int snapW = 0, snapH = 0;
    int resIndex = 2;                              // index into RESOLUTIONS (128 x 72)
    int imgW = 128, imgH = 72;
    std::vector<unsigned char> clean, input, output;
    GLuint texIn = 0, texOut = 0;
    Shader imageShader;

    // ---- operation ----
    LabOp op = OP_CONVOLVE;
    LabKernel kernel;
    int presetIndex = 2;
    bool gray = false;
    int channel = 0;                               // channel shown in the maths (RGB mode)
    LabNoise noise = NOISE_NONE;
    bool dirty = true;                             // output must be recomputed
    bool inputDirty = true;                        // input must be rebuilt from the snapshot
    double psnrIn = 0.0, psnrOut = 0.0;
    int hist[3][256] = {};
    int cdf[3][256] = {};

    // ---- scan / focus ----
    int reveal = 0;                                // output pixels shown so far
    bool playing = true;
    int speedIndex = 3;                            // 30 pixels per second
    float accum = 0.0f;
    glm::ivec2 focus{ 0, 0 };

    // ---- editing ----
    int editCell = -1;                             // 0..24 kernel weight, 100 = divisor
    std::string editBuffer;

    // ---- mouse ----
    glm::vec2 mouse{ -1.0f };
    bool clickPending = false;
    float scrollPending = 0.0f;

    void loadPreset(int index);
    void rebuildInput();
    void compute();
    void uploadTextures();
    void restartScan();
    void setFocusFromReveal();
    void commitEdit();
    void applyAutoDivisor();

    int inAt(int x, int y, int c) const;           // clamp-to-edge
    int outAt(int x, int y, int c) const;
    int convolveAt(int x, int y, int c, float* sumOut = nullptr) const;
    void windowValues(int x, int y, int c, std::vector<int>& vals) const;

    bool button(Overlay2D& ui, float x, float y, float w, float h, const std::string& label, bool on, float ts);
    void drawImage(const GeometryManager& geo, GLuint tex, float x, float y, float w, float h, int W, int H, int revealCount);
};
