#pragma once

// Convolution kernel table shared by the GPU filters (dip.frag) and the
// step-by-step CPU convolution visualizer, so both always use identical weights.
//
// Weights are stored as integers with a divisor so they can be shown exactly
// on screen: effective weight = w[i] / divisor.

struct ConvKernel {
    const char* name;      // short name shown in the UI
    int size;              // 3 or 5 (square kernel)
    float w[25];           // row-major integer weights (first size*size used)
    float divisor;
    bool absolute;         // take |sum| (edge detectors produce signed output)
};

static const ConvKernel CONV_KERNELS[] = {
    { "Gaussian 3x3", 3,
      { 1, 2, 1,
        2, 4, 2,
        1, 2, 1 }, 16.0f, false },
    { "Gaussian 5x5", 5,
      { 1,  4,  7,  4, 1,
        4, 16, 26, 16, 4,
        7, 26, 41, 26, 7,
        4, 16, 26, 16, 4,
        1,  4,  7,  4, 1 }, 273.0f, false },
    { "Box Blur 3x3", 3,
      { 1, 1, 1,
        1, 1, 1,
        1, 1, 1 }, 9.0f, false },
    { "Sharpen 3x3", 3,
      {  0, -1,  0,
        -1,  5, -1,
         0, -1,  0 }, 1.0f, false },
    { "Laplacian Edge 3x3", 3,
      { 0,  1, 0,
        1, -4, 1,
        0,  1, 0 }, 1.0f, true },
    { "Sobel X 3x3", 3,
      { -1, 0, 1,
        -2, 0, 2,
        -1, 0, 1 }, 1.0f, true },
    { "Emboss 3x3", 3,
      { -2, -1, 0,
        -1,  1, 1,
         0,  1, 2 }, 1.0f, false },
};

static const int CONV_KERNEL_COUNT = sizeof(CONV_KERNELS) / sizeof(CONV_KERNELS[0]);
