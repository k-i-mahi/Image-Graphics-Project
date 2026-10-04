#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <array>
#include <string>
#include "Shader.h"
#include "Geometry.h"
#include "Kernels.h"
#include "Downsampler.h"

enum ViewMode {
    VIEW_PRISTINE = 1,
    VIEW_CCTV_RAW = 2,
    VIEW_DEGRADED = 3,
    VIEW_ENHANCED = 4,
    VIEW_DEPTH_MAP = 5,
    VIEW_DOF = 6,
    VIEW_SPLIT_SCREEN = 7,
    VIEW_EDGES = 8,
    VIEW_NIGHT_VISION = 9
};

enum DenoiseMode { DENOISE_KERNEL = 0, DENOISE_MEDIAN = 1, DENOISE_BILATERAL = 2 };

// Live image processing of the camera frame (dip.frag on a full-screen quad).
//
// Views 4 and 7 run in two passes: the denoised sensor image is first rendered
// into a stage texture; a 240 px wide copy of it is read back, its luminance
// histogram gives a 256-entry equalization lookup table (imgops, unit-tested),
// and the second pass applies that table: real histogram equalization.
class DIPProcessor {
public:
    ViewMode currentViewMode;
    int filterIndex;                 // index into CONV_KERNELS (Kernels.h)
    DenoiseMode denoise;

    // Kernel sent from the Image Operation Lab (U key in the lab)
    bool useCustomKernel;
    float customKernel[25];          // already divided by the divisor
    int customSize;
    bool customAbs;
    std::string customName;

    // Degradation parameters
    float noiseIntensity;
    float lowLightDampening;

    // Enhancement parameters
    bool enableHistogramStretch;     // H: histogram equalization on/off
    bool temporalNR;                 // U: motion-adaptive temporal noise reduction (views 4 / 7)

    // Depth of Field parameters
    float dofFocalDepth;
    float dofAlpha;
    float nearPlane;
    float farPlane;

    // Split screen separator
    float splitPosition; // 0.0 to 1.0 (default 0.5)

    DIPProcessor();
    ~DIPProcessor();

    bool initGL(int width, int height);
    void resize(int width, int height);
    void renderPostProcess(const Shader& dipShader,
                          const GeometryManager& geo,
                          GLuint colorTex,
                          GLuint depthTex,
                          GLuint bloomTex,
                          float bloomStrength,
                          int screenW,
                          int screenH,
                          float runTime);

    const char* getViewModeName() const;
    std::string getFilterName() const;
    const std::array<int, 256>& lastHistogram() const { return histogram; }
    const std::array<uint8_t, 256>& lastLUT() const { return lut; }

private:
    GLuint stageFBO[2], stageTex[2], lutTex;   // ping-pong: this frame / previous frame
    int cur;                                    // index of this frame's stage texture
    bool historyValid;
    int stageW, stageH;
    std::array<int, 256> histogram{};
    std::array<uint8_t, 256> lut{};
    Downsampler reader;

    void setUniforms(const Shader& s, int screenW, int screenH, float runTime, float bloomStrength) const;
    void updateEqualizationLUT();
};
