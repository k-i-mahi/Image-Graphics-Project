#include "DIPProcessor.h"
#include "ImageOps.h"
#include <algorithm>
#include <vector>

DIPProcessor::DIPProcessor()
    : currentViewMode(VIEW_PRISTINE),
      filterIndex(0),
      denoise(DENOISE_BILATERAL),
      useCustomKernel(false),
      customKernel{ 0 },
      customSize(3),
      customAbs(false),
      noiseIntensity(0.10f),
      lowLightDampening(0.38f),
      enableHistogramStretch(true),
      temporalNR(true),
      dofFocalDepth(22.0f),
      dofAlpha(0.32f),
      nearPlane(0.1f),
      farPlane(400.0f),
      splitPosition(0.5f),
      stageFBO{ 0, 0 }, stageTex{ 0, 0 }, lutTex(0), cur(0), historyValid(false), stageW(0), stageH(0) {
    for (int i = 0; i < 256; ++i) lut[i] = static_cast<uint8_t>(i);
}

DIPProcessor::~DIPProcessor() {}

bool DIPProcessor::initGL(int width, int height) {
    glGenFramebuffers(2, stageFBO);
    glGenTextures(2, stageTex);
    glGenTextures(1, &lutTex);
    glBindTexture(GL_TEXTURE_2D, lutTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 256, 1, 0, GL_RED, GL_UNSIGNED_BYTE, lut.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    resize(width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, stageFBO[0]);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}

void DIPProcessor::resize(int width, int height) {
    if (width == stageW && height == stageH) return;
    stageW = width;
    stageH = height;
    historyValid = false;
    for (int i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, stageTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, stageFBO[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, stageTex[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void DIPProcessor::setUniforms(const Shader& s, int screenW, int screenH, float runTime, float bloomStrength) const {
    s.setInt("uColorTexture", 0);
    s.setInt("uDepthTexture", 1);
    s.setInt("uBloom", 2);
    s.setInt("uEqLUT", 3);
    s.setInt("uStage", 4);
    s.setInt("uPrevStage", 5);
    s.setFloat("uBloomStrength", bloomStrength);

    // Convolution kernel: from the lab, or the shared table (Kernels.h)
    float weights[25] = { 0.0f };
    int size = 3;
    bool absolute = false;
    if (useCustomKernel) {
        std::copy(customKernel, customKernel + 25, weights);
        size = customSize;
        absolute = customAbs;
    } else {
        const ConvKernel& k = CONV_KERNELS[filterIndex];
        for (int i = 0; i < k.size * k.size; ++i) weights[i] = k.w[i] / k.divisor;
        size = k.size;
        absolute = k.absolute;
    }
    glUniform1fv(s.loc("uKernel"), 25, weights);
    s.setInt("uKernelSize", size);
    s.setBool("uKernelAbs", absolute);
    s.setInt("uDenoise", static_cast<int>(denoise));

    s.setFloat("uNoiseIntensity", noiseIntensity);
    s.setFloat("uLowLightDampening", lowLightDampening);
    s.setFloat("uTime", runTime);
    s.setVec2("uScreenResolution", static_cast<float>(screenW), static_cast<float>(screenH));
    s.setBool("uEnableEqualization", enableHistogramStretch);
    s.setFloat("uFocalDepth", dofFocalDepth);
    s.setFloat("uDofAlpha", dofAlpha);
    s.setFloat("uNearPlane", nearPlane);
    s.setFloat("uFarPlane", farPlane);
    s.setFloat("uSplitPosition", splitPosition);
}

// Reads a 240 px wide copy of the stage image, builds the luminance
// histogram and the equalization LUT, uploads the LUT (256 x 1 texture).
void DIPProcessor::updateEqualizationLUT() {
    int w = std::min(240, stageW), h = std::max(1, w * stageH / std::max(1, stageW));
    std::vector<uint8_t> rgba;
    reader.read(stageFBO[cur], stageW, stageH, w, h, rgba);

    std::vector<uint8_t> luma(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < luma.size(); ++i)
        luma[i] = static_cast<uint8_t>(imgops::luminance(rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]));
    imgops::Histogram hist = imgops::histogramOf(luma);
    histogram = hist.count;
    lut = imgops::equalizationLUT(hist);

    glBindTexture(GL_TEXTURE_2D, lutTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RED, GL_UNSIGNED_BYTE, lut.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void DIPProcessor::renderPostProcess(const Shader& dipShader,
                                     const GeometryManager& geo,
                                     GLuint colorTex,
                                     GLuint depthTex,
                                     GLuint bloomTex,
                                     float bloomStrength,
                                     int screenW,
                                     int screenH,
                                     float runTime) {
    glDisable(GL_DEPTH_TEST);
    dipShader.use();
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, colorTex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, depthTex);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, bloomTex);
    setUniforms(dipShader, screenW, screenH, runTime, bloomTex ? bloomStrength : 0.0f);

    bool twoPass = (currentViewMode == VIEW_ENHANCED || currentViewMode == VIEW_SPLIT_SCREEN);
    if (!twoPass) historyValid = false;   // stale history after leaving views 4 / 7
    if (twoPass) {
        // Pass 1: denoised sensor image -> this frame's stage texture. The previous
        // frame's stage (unit 5) feeds the temporal noise reduction.
        resize(screenW, screenH);
        cur ^= 1;
        glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, stageTex[cur ^ 1]);
        glActiveTexture(GL_TEXTURE0);
        glBindFramebuffer(GL_FRAMEBUFFER, stageFBO[cur]);
        glViewport(0, 0, screenW, screenH);
        dipShader.setInt("uViewMode", 100);
        dipShader.setBool("uUseStage", false);
        dipShader.setBool("uTemporal", temporalNR && historyValid);
        geo.drawQuad();
        historyValid = true;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (enableHistogramStretch) updateEqualizationLUT();
        dipShader.use();
    }

    // Pass 2 (or the only pass): the selected view. Rebind every unit: the LUT
    // update above changed the texture bound to the active unit.
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, colorTex);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, bloomTex);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, lutTex);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, stageTex[cur]);
    glActiveTexture(GL_TEXTURE0);
    dipShader.setBool("uUseStage", twoPass);
    dipShader.setInt("uViewMode", static_cast<int>(currentViewMode));
    geo.drawQuad();

    glEnable(GL_DEPTH_TEST);
}

const char* DIPProcessor::getViewModeName() const {
    switch (currentViewMode) {
        case VIEW_PRISTINE:     return "1: Pristine 3D Scene";
        case VIEW_CCTV_RAW:     return "2: CCTV Live Camera";
        case VIEW_DEGRADED:     return "3: Degraded Sensor (Low-Light + Gaussian Noise)";
        case VIEW_ENHANCED:     return "4: Enhanced (Denoise + Histogram Equalization)";
        case VIEW_DEPTH_MAP:    return "5: Depth Buffer Map";
        case VIEW_DOF:          return "6: Depth-of-Field (Equation 2)";
        case VIEW_SPLIT_SCREEN: return "7: Split Screen (Degraded | Enhanced)";
        case VIEW_EDGES:        return "8: Sobel Edge Detection";
        case VIEW_NIGHT_VISION: return "9: Night Vision";
        default:                return "Unknown Mode";
    }
}

std::string DIPProcessor::getFilterName() const {
    if (denoise == DENOISE_MEDIAN) return "Median 3x3";
    if (denoise == DENOISE_BILATERAL) return "Bilateral 5x5";
    return useCustomKernel ? customName : std::string(CONV_KERNELS[filterIndex].name);
}
