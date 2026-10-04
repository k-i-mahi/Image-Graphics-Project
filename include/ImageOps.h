#pragma once

// ---------------------------------------------------------------------------
// ImageOps — the image processing algorithms of the project as plain C++
// (no OpenGL), so they can be unit-tested and reused by the Image Operation
// Lab and the CCTV motion detector.
//
// Images are 8-bit RGB, interleaved, row 0 at the TOP. Pixel reads outside
// the image use clamp-to-edge. Channel 3 means luminance
// Y = 0.299 R + 0.587 G + 0.114 B.
// ---------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <vector>

namespace imgops {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;   // w * h * 3

    Image() = default;
    Image(int width, int height, uint8_t fill = 0) : w(width), h(height), px(static_cast<size_t>(width) * height * 3, fill) {}

    size_t index(int x, int y) const { return (static_cast<size_t>(y) * w + x) * 3; }
    int at(int x, int y, int c) const;                 // clamp-to-edge, c = 0..3
    void set(int x, int y, int r, int g, int b);
    bool empty() const { return px.empty(); }
};

int luminance(int r, int g, int b);

// ---- convolution --------------------------------------------------------
struct Kernel {
    int size = 3;                // 3 or 5
    float w[25] = { 0 };         // row-major
    float divisor = 1.0f;
    bool absolute = false;       // |v|   (edge detectors)
    bool offset128 = false;      // v + 128 (emboss)

    float sum() const;
    void autoDivisor();          // divisor = sum of weights, or 1 when the sum is 0
};

// out(x,y) = clamp(round( post( sum_ij w(i,j) * in(x+i-r, y+j-r) / divisor ) ))
int convolvePixel(const Image& in, const Kernel& k, int x, int y, int c, float* rawSum = nullptr);
void convolve(const Image& in, const Kernel& k, Image& out, bool grey = false);

// ---- rank (order-statistic) filters ---------------------------------------
enum class Rank { Median, Min, Max };
void windowSorted(const Image& in, int size, int x, int y, int c, std::vector<int>& sorted);
void rankFilter(const Image& in, int size, Rank rank, Image& out, bool grey = false);

// ---- histogram equalization -----------------------------------------------
struct Histogram {
    std::array<int, 256> count{};
    std::array<int, 256> cdf{};
    int cdfMin = 0;          // first non-zero cdf value
    int total = 0;
};
Histogram lumaHistogram(const Image& in);
Histogram histogramOf(const std::vector<uint8_t>& values);
// lut[v] = round(255 * (cdf(v) - cdf_min) / (N - cdf_min))
std::array<uint8_t, 256> equalizationLUT(const Histogram& h);
// Equalizes luminance and scales RGB by Y'/Y so colours keep their hue
void equalize(const Image& in, Image& out, Histogram* histOut = nullptr);

// ---- noise and quality ------------------------------------------------------
void addGaussianNoise(Image& img, double sigma, uint32_t seed, bool grey);    // Box-Muller
void addSaltPepper(Image& img, double probability, uint32_t seed);
double mse(const Image& a, const Image& b);
double psnr(const Image& a, const Image& b);          // 99 when identical

// ---- binary masks (motion detection) ---------------------------------------
struct Mask {
    int w = 0, h = 0;
    std::vector<uint8_t> m;   // 0 or 1
    Mask() = default;
    Mask(int width, int height) : w(width), h(height), m(static_cast<size_t>(width) * height, 0) {}
    uint8_t at(int x, int y) const { return (x < 0 || y < 0 || x >= w || y >= h) ? 0 : m[static_cast<size_t>(y) * w + x]; }
};
Mask erode(const Mask& in);     // 3x3: keep a pixel only if all 9 neighbours are set
Mask dilate(const Mask& in);    // 3x3: set a pixel if any neighbour is set

struct Blob { int x0, y0, x1, y1, area; };   // inclusive pixel bounds
// 8-connected component labelling (two-pass with union-find)
std::vector<Blob> connectedComponents(const Mask& mask, int minArea);

} // namespace imgops
