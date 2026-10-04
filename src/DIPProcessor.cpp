#include "DIPProcessor.h"

DIPProcessor::DIPProcessor()
    : currentViewMode(VIEW_PRISTINE),
      filterIndex(0),
      noiseIntensity(0.18f),
      lowLightDampening(0.38f),
      contrastStretchFactor(2.2f),
      enableHistogramStretch(true),
      dofFocalDepth(22.0f),
      dofAlpha(0.32f),
      nearPlane(0.1f),
      farPlane(400.0f),
      splitPosition(0.5f) {}

DIPProcessor::~DIPProcessor() {}

void DIPProcessor::init() {}

void DIPProcessor::renderPostProcess(const Shader& dipShader, 
                                     const GeometryManager& geo, 
                                     GLuint colorTex, 
                                     GLuint depthTex, 
                                     int screenW, 
                                     int screenH, 
                                     float runTime) {
    glDisable(GL_DEPTH_TEST);
    dipShader.use();

    // Bind color and depth textures from FBO
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    dipShader.setInt("uColorTexture", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, depthTex);
    dipShader.setInt("uDepthTexture", 1);

    // Uniforms
    dipShader.setInt("uViewMode", static_cast<int>(currentViewMode));
    // Convolution kernel (same table the step-by-step visualizer uses)
    const ConvKernel& k = CONV_KERNELS[filterIndex];
    float weights[25] = { 0.0f };
    for (int i = 0; i < k.size * k.size; ++i) weights[i] = k.w[i] / k.divisor;
    glUniform1fv(glGetUniformLocation(dipShader.ID, "uKernel"), 25, weights);
    dipShader.setInt("uKernelSize", k.size);
    dipShader.setBool("uKernelAbs", k.absolute);
    dipShader.setFloat("uNoiseIntensity", noiseIntensity);
    dipShader.setFloat("uLowLightDampening", lowLightDampening);
    dipShader.setFloat("uTime", runTime);
    dipShader.setVec2("uScreenResolution", static_cast<float>(screenW), static_cast<float>(screenH));

    dipShader.setFloat("uContrastStretch", contrastStretchFactor);
    dipShader.setBool("uEnableEqualization", enableHistogramStretch);

    dipShader.setFloat("uFocalDepth", dofFocalDepth);
    dipShader.setFloat("uDofAlpha", dofAlpha);
    dipShader.setFloat("uNearPlane", nearPlane);
    dipShader.setFloat("uFarPlane", farPlane);

    dipShader.setFloat("uSplitPosition", splitPosition);

    // Draw full-screen quad
    geo.drawQuad();

    glEnable(GL_DEPTH_TEST);
}

const char* DIPProcessor::getViewModeName() const {
    switch (currentViewMode) {
        case VIEW_PRISTINE:     return "1: Pristine 3D Scene";
        case VIEW_CCTV_RAW:     return "2: CCTV Live Camera (Tactical Feed)";
        case VIEW_DEGRADED:     return "3: Degraded Sensor (Low-Light + Gaussian Noise)";
        case VIEW_ENHANCED:     return "4: Enhanced DIP (Spatial Convolution + Contrast Stretch)";
        case VIEW_DEPTH_MAP:    return "5: Depth Buffer Map (Linearized View-Space)";
        case VIEW_DOF:          return "6: Depth-Driven Depth-of-Field (Equation 2)";
        case VIEW_SPLIT_SCREEN: return "7: Split-Screen Comparison (Degraded vs Enhanced)";
        case VIEW_EDGES:        return "8: Sobel Edge Detection";
        case VIEW_NIGHT_VISION: return "9: Night Vision (Image Intensifier)";
        case VIEW_SIDE_BY_SIDE: return "0: Side-by-Side Filter Compare";
        default:                return "Unknown Mode";
    }
}

const char* DIPProcessor::getFilterName() const {
    return CONV_KERNELS[filterIndex].name;
}
