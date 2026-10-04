// Unit tests for ImageOps (no framework needed: run the executable or `ctest`).
#include "ImageOps.h"
#include <cmath>
#include <array>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace imgops;

static int failures = 0, checks = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (!(cond)) { ++failures; std::printf("  FAILED %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static Image ramp(int w, int h) {                      // grey ramp 0..255 left to right
    Image img(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int v = x * 255 / (w - 1);
            img.set(x, y, v, v, v);
        }
    return img;
}

static Kernel kernel3(std::initializer_list<float> w, bool autoDiv = true) {
    Kernel k;
    k.size = 3;
    int i = 0;
    for (float v : w) k.w[i++] = v;
    if (autoDiv) k.autoDivisor();
    return k;
}

static void testClampToEdge() {
    Image img = ramp(8, 4);
    CHECK(img.at(-5, 0, 0) == 0);
    CHECK(img.at(100, 2, 0) == 255);
    CHECK(img.at(3, -1, 0) == img.at(3, 0, 0));
}

static void testLuminance() {
    CHECK(luminance(255, 255, 255) == 255);
    CHECK(luminance(0, 0, 0) == 0);
    CHECK(luminance(255, 0, 0) == 76);      // 0.299 * 255 = 76.2
}

static void testIdentityKernel() {
    Image in = ramp(16, 9), out;
    convolve(in, kernel3({ 0, 0, 0, 0, 1, 0, 0, 0, 0 }), out);
    CHECK(out.px == in.px);
}

static void testBoxBlurOfConstantIsConstant() {
    Image in(10, 10, 77), out;
    convolve(in, kernel3({ 1, 1, 1, 1, 1, 1, 1, 1, 1 }), out);
    bool same = true;
    for (uint8_t v : out.px) same = same && v == 77;
    CHECK(same);
}

static void testHandComputedGaussian() {
    // 3x3 neighbourhood (grey):  10 20 30 / 40 50 60 / 70 80 90, Gaussian 1 2 1 / 2 4 2 / 1 2 1 (/16)
    Image in(3, 3);
    int v[9] = { 10, 20, 30, 40, 50, 60, 70, 80, 90 };
    for (int i = 0; i < 9; ++i) in.set(i % 3, i / 3, v[i], v[i], v[i]);
    Kernel k = kernel3({ 1, 2, 1, 2, 4, 2, 1, 2, 1 });
    CHECK(k.divisor == 16.0f);
    float sum = 0.0f;
    int out = convolvePixel(in, k, 1, 1, 0, &sum);
    // 10 + 40 + 30 + 80 + 200 + 120 + 70 + 160 + 90 = 800 ; 800 / 16 = 50
    CHECK(std::fabs(sum - 800.0f) < 1e-3f);
    CHECK(out == 50);
}

static void testSobelAbsoluteAndClamp() {
    Image in = ramp(16, 4);                                  // gradient of 17 per pixel
    Kernel sx = kernel3({ -1, 0, 1, -2, 0, 2, -1, 0, 1 });
    sx.absolute = true;
    // inside: (17 * 2) * (1 + 2 + 1) = 136
    CHECK(convolvePixel(in, sx, 8, 2, 0) == 136);
    Kernel neg = kernel3({ 0, 0, 0, 0, -1, 0, 0, 0, 0 }, false);
    CHECK(convolvePixel(in, neg, 8, 2, 0) == 0);             // negative clamps to 0
    neg.absolute = true;
    CHECK(convolvePixel(in, neg, 8, 2, 0) == in.at(8, 2, 0));
}

static void testEmbossOffset() {
    Image in(5, 5, 100);
    Kernel e = kernel3({ -2, -1, 0, -1, 0, 1, 0, 1, 2 });
    e.offset128 = true;
    CHECK(e.divisor == 1.0f);                                // weights sum to 0 -> divisor 1
    CHECK(convolvePixel(in, e, 2, 2, 0) == 128);              // flat area -> mid grey
}

static void testMedianRemovesImpulse() {
    Image in(7, 7, 120), out;
    in.set(3, 3, 255, 255, 255);                             // salt
    in.set(1, 5, 0, 0, 0);                                   // pepper
    rankFilter(in, 3, Rank::Median, out);
    CHECK(out.at(3, 3, 0) == 120);
    CHECK(out.at(1, 5, 0) == 120);
}

static void testMinMax() {
    Image in(5, 5, 50), out;
    in.set(2, 2, 200, 200, 200);
    rankFilter(in, 3, Rank::Max, out);
    CHECK(out.at(1, 1, 0) == 200 && out.at(3, 3, 0) == 200 && out.at(0, 0, 0) == 50);
    rankFilter(in, 3, Rank::Min, out);
    CHECK(out.at(2, 2, 0) == 50);
    std::vector<int> s;
    windowSorted(in, 3, 2, 2, 0, s);
    CHECK(s.size() == 9 && s.front() == 50 && s.back() == 200);
}

static void testHistogramEqualization() {
    // two grey levels 100 and 110 -> stretched to 0 and 255
    Image in(4, 1), out;
    in.set(0, 0, 100, 100, 100); in.set(1, 0, 100, 100, 100);
    in.set(2, 0, 110, 110, 110); in.set(3, 0, 110, 110, 110);
    Histogram h;
    equalize(in, out, &h);
    CHECK(h.total == 4 && h.cdfMin == 2 && h.cdf[110] == 4);
    CHECK(out.at(0, 0, 0) == 0);
    CHECK(out.at(3, 0, 0) == 255);
    // the LUT is monotonic
    Image r = ramp(64, 4);
    auto lut = equalizationLUT(lumaHistogram(r));
    bool mono = true;
    for (int v = 1; v < 256; ++v) mono = mono && lut[v] >= lut[v - 1];
    CHECK(mono);
    // a single grey level is left unchanged
    Image flat(3, 3, 90);
    equalize(flat, out);
    CHECK(out.at(1, 1, 0) == 90);
}

static void testPsnr() {
    Image a(10, 10, 100), b(10, 10, 100);
    CHECK(psnr(a, b) == 99.0);
    b.px[0] = 110;                                           // one sample off by 10
    double expectedMse = 100.0 / 300.0;
    CHECK(std::fabs(mse(a, b) - expectedMse) < 1e-9);
    CHECK(std::fabs(psnr(a, b) - 10.0 * std::log10(255.0 * 255.0 / expectedMse)) < 1e-9);
}

static void testNoiseIsDeterministicAndBounded() {
    Image a(32, 32, 128), b(32, 32, 128);
    addGaussianNoise(a, 25.0, 7u, false);
    addGaussianNoise(b, 25.0, 7u, false);
    CHECK(a.px == b.px);
    double mean = 0.0;
    for (uint8_t v : a.px) mean += v;
    mean /= a.px.size();
    CHECK(std::fabs(mean - 128.0) < 3.0);                    // zero-mean noise
    Image sp(100, 100, 128);
    addSaltPepper(sp, 0.1, 3u);
    int hits = 0;
    for (size_t k = 0; k < sp.px.size(); k += 3) hits += sp.px[k] == 0 || sp.px[k] == 255;
    CHECK(hits > 700 && hits < 1300);                         // ~10 % of 10000
}

static void testMorphologyAndComponents() {
    Mask m(20, 10);
    auto fill = [&m](int x0, int y0, int x1, int y1) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) m.m[static_cast<size_t>(y) * m.w + x] = 1;
    };
    fill(1, 1, 5, 5);          // blob A (25 px)
    fill(10, 2, 13, 3);        // blob B (8 px)
    m.m[9 * 20 + 18] = 1;      // single noise pixel
    std::vector<Blob> blobs = connectedComponents(m, 1);
    CHECK(blobs.size() == 3);
    CHECK(blobs[0].area == 25 && blobs[0].x0 == 1 && blobs[0].x1 == 5);
    CHECK(connectedComponents(m, 5).size() == 2);           // area filter
    Mask opened = dilate(erode(m));                          // opening removes the noise pixel
    CHECK(opened.at(18, 9) == 0);
    CHECK(opened.at(3, 3) == 1);
    // diagonal pixels are one 8-connected component
    Mask d(4, 4);
    d.m[0] = d.m[5] = d.m[10] = d.m[15] = 1;
    CHECK(connectedComponents(d, 1).size() == 1);
    // U shape: two arms joined at the bottom must merge into one label
    Mask u(5, 4);
    for (int y = 0; y < 4; ++y) { u.m[y * 5 + 0] = 1; u.m[y * 5 + 4] = 1; }
    for (int x = 0; x < 5; ++x) u.m[3 * 5 + x] = 1;
    std::vector<Blob> ub = connectedComponents(u, 1);
    CHECK(ub.size() == 1 && ub[0].area == 11);
}

static void testOtsu() {
    // bimodal: half the pixels at 40, half at 200 -> threshold separates them
    Image img(10, 10);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) { int v = x < 5 ? 40 : 200; img.set(x, y, v, v, v); }
    std::array<double, 256> sb{};
    int t = otsuThreshold(lumaHistogram(img), &sb);
    CHECK(t >= 40 && t < 200);
    CHECK(sb[t] > 0.0 && sb[t] >= sb[10] && sb[t] >= sb[220]);
    // sigma_B^2 at the optimum: w0 = w1 = 0.5, (mu0 - mu1)^2 = 160^2  ->  0.25 * 25600 = 6400
    CHECK(std::fabs(sb[t] - 6400.0) < 1e-6);
    Image out;
    threshold(img, t, out);
    CHECK(out.at(2, 2, 0) == 0 && out.at(8, 8, 0) == 255);
}

static void testFrequencyFilter() {
    Image in = ramp(12, 8), out;
    FreqResult info;
    frequencyFilter(in, FreqFilter::IdealLow, 1000.0f, out, true, &info);    // passes everything
    bool same = true;
    for (int y = 0; y < in.h; ++y)
        for (int x = 0; x < in.w; ++x) same = same && std::abs(out.at(x, y, 0) - in.at(x, y, 0)) <= 1;
    CHECK(same);                                                            // DFT -> IDFT round trip
    CHECK(info.logMagnitude.size() == 96 && info.response.size() == 96);
    CHECK(info.logMagnitude[4 * 12 + 6] == 1.0f);                           // DC term is the strongest, at the centre
    Image flat(8, 8, 90);
    frequencyFilter(flat, FreqFilter::GaussianLow, 1.5f, out, true);
    CHECK(out.at(3, 3, 0) == 90);                                           // low-pass keeps a constant image
    frequencyFilter(flat, FreqFilter::IdealHigh, 0.5f, out, true);
    CHECK(out.at(3, 3, 0) == 128);                                          // high-pass removes it (+128 offset)
    CHECK(filterResponse(FreqFilter::GaussianLow, 0.0f, 5.0f) == 1.0f);
    CHECK(std::fabs(filterResponse(FreqFilter::GaussianHigh, 5.0f, 5.0f) - (1.0f - std::exp(-0.5f))) < 1e-6f);
}

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        { "clamp-to-edge", testClampToEdge },
        { "luminance", testLuminance },
        { "identity kernel", testIdentityKernel },
        { "box blur of a constant image", testBoxBlurOfConstantIsConstant },
        { "hand-computed Gaussian", testHandComputedGaussian },
        { "Sobel abs() and clamping", testSobelAbsoluteAndClamp },
        { "emboss +128", testEmbossOffset },
        { "median removes impulses", testMedianRemovesImpulse },
        { "min / max filters", testMinMax },
        { "histogram equalization", testHistogramEqualization },
        { "PSNR / MSE", testPsnr },
        { "noise generators", testNoiseIsDeterministicAndBounded },
        { "morphology + connected components", testMorphologyAndComponents },
        { "Otsu thresholding", testOtsu },
        { "frequency-domain filtering (DFT)", testFrequencyFilter },
    };
    for (const auto& t : tests) {
        int before = failures;
        t.second();
        std::printf("[%s] %s\n", failures == before ? " OK " : "FAIL", t.first.c_str());
    }
    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
