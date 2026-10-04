#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include "Shader.h"
#include "Geometry.h"
#include "Kernels.h"

enum ViewMode {
    VIEW_PRISTINE = 1,
    VIEW_CCTV_RAW = 2,
    VIEW_DEGRADED = 3,
    VIEW_ENHANCED = 4,
    VIEW_DEPTH_MAP = 5,
    VIEW_DOF = 6,
    VIEW_SPLIT_SCREEN = 7,
    VIEW_EDGES = 8,
    VIEW_NIGHT_VISION = 9,
    VIEW_SIDE_BY_SIDE = 10
};


class DIPProcessor {
public:
    ViewMode currentViewMode;
    int filterIndex; // index into CONV_KERNELS (Kernels.h)

    // Degradation parameters
    float noiseIntensity;
    float lowLightDampening;

    // Enhancement parameters
    float contrastStretchFactor;
    bool enableHistogramStretch;

    // Depth of Field parameters
    float dofFocalDepth;
    float dofAlpha;
    float nearPlane;
    float farPlane;

    // Split screen separator
    float splitPosition; // 0.0 to 1.0 (default 0.5)

    DIPProcessor();
    ~DIPProcessor();

    void init();
    void renderPostProcess(const Shader& dipShader, 
                          const GeometryManager& geo, 
                          GLuint colorTex, 
                          GLuint depthTex, 
                          int screenW, 
                          int screenH, 
                          float runTime);

    const char* getViewModeName() const;
    const char* getFilterName() const;
};
