#include "ImageOps.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace imgops {

int luminance(int r, int g, int b) {
    return static_cast<int>(0.299 * r + 0.587 * g + 0.114 * b + 0.5);
}

int Image::at(int x, int y, int c) const {
    x = std::clamp(x, 0, w - 1);
    y = std::clamp(y, 0, h - 1);
    size_t k = index(x, y);
    if (c < 3) return px[k + c];
    return luminance(px[k], px[k + 1], px[k + 2]);
}

void Image::set(int x, int y, int r, int g, int b) {
    size_t k = index(x, y);
    px[k] = static_cast<uint8_t>(r);
    px[k + 1] = static_cast<uint8_t>(g);
    px[k + 2] = static_cast<uint8_t>(b);
}

static uint8_t clampByte(double v) {
    return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(v)), 0, 255));
}

// ---------------------------------------------------------------------------
// Convolution
// ---------------------------------------------------------------------------
float Kernel::sum() const {
    float s = 0.0f;
    for (int i = 0; i < size * size; ++i) s += w[i];
    return s;
}

void Kernel::autoDivisor() {
    float s = sum();
    divisor = std::fabs(s) < 1e-6f ? 1.0f : s;
}

int convolvePixel(const Image& in, const Kernel& k, int x, int y, int c, float* rawSum) {
    const int r = k.size / 2;
    float s = 0.0f;
    for (int j = 0; j < k.size; ++j)
        for (int i = 0; i < k.size; ++i) s += k.w[j * k.size + i] * in.at(x + i - r, y + j - r, c);
    if (rawSum) *rawSum = s;
    float v = s / k.divisor;
    if (k.absolute) v = std::fabs(v);
    if (k.offset128) v += 128.0f;
    return clampByte(v);
}

void convolve(const Image& in, const Kernel& k, Image& out, bool grey) {
    out = Image(in.w, in.h);
    for (int y = 0; y < in.h; ++y)
        for (int x = 0; x < in.w; ++x) {
            if (grey) {
                int v = convolvePixel(in, k, x, y, 0);
                out.set(x, y, v, v, v);
            } else {
                out.set(x, y, convolvePixel(in, k, x, y, 0), convolvePixel(in, k, x, y, 1), convolvePixel(in, k, x, y, 2));
            }
        }
}

// ---------------------------------------------------------------------------
// Rank filters
// ---------------------------------------------------------------------------
void windowSorted(const Image& in, int size, int x, int y, int c, std::vector<int>& sorted) {
    const int r = size / 2;
    sorted.clear();
    for (int j = -r; j <= r; ++j)
        for (int i = -r; i <= r; ++i) sorted.push_back(in.at(x + i, y + j, c));
    std::sort(sorted.begin(), sorted.end());
}

void rankFilter(const Image& in, int size, Rank rank, Image& out, bool grey) {
    out = Image(in.w, in.h);
    std::vector<int> v;
    auto pick = [&](int x, int y, int c) {
        windowSorted(in, size, x, y, c, v);
        return rank == Rank::Median ? v[v.size() / 2] : (rank == Rank::Min ? v.front() : v.back());
    };
    for (int y = 0; y < in.h; ++y)
        for (int x = 0; x < in.w; ++x) {
            if (grey) {
                int g = pick(x, y, 0);
                out.set(x, y, g, g, g);
            } else {
                out.set(x, y, pick(x, y, 0), pick(x, y, 1), pick(x, y, 2));
            }
        }
}

// ---------------------------------------------------------------------------
// Histogram equalization
// ---------------------------------------------------------------------------
static void finishHistogram(Histogram& h) {
    int run = 0;
    h.cdfMin = 0;
    for (int v = 0; v < 256; ++v) {
        run += h.count[v];
        h.cdf[v] = run;
        if (h.cdfMin == 0 && run > 0) h.cdfMin = run;
    }
    h.total = run;
}

Histogram lumaHistogram(const Image& in) {
    Histogram h;
    for (size_t k = 0; k < in.px.size(); k += 3) h.count[luminance(in.px[k], in.px[k + 1], in.px[k + 2])]++;
    finishHistogram(h);
    return h;
}

Histogram histogramOf(const std::vector<uint8_t>& values) {
    Histogram h;
    for (uint8_t v : values) h.count[v]++;
    finishHistogram(h);
    return h;
}

std::array<uint8_t, 256> equalizationLUT(const Histogram& h) {
    std::array<uint8_t, 256> lut{};
    for (int v = 0; v < 256; ++v) {
        if (h.total <= h.cdfMin) lut[v] = static_cast<uint8_t>(v);   // single grey level: leave unchanged
        else lut[v] = clampByte(255.0 * (h.cdf[v] - h.cdfMin) / (h.total - h.cdfMin));
    }
    return lut;
}

void equalize(const Image& in, Image& out, Histogram* histOut) {
    Histogram h = lumaHistogram(in);
    std::array<uint8_t, 256> lut = equalizationLUT(h);
    out = Image(in.w, in.h);
    for (size_t k = 0; k < in.px.size(); k += 3) {
        int y = luminance(in.px[k], in.px[k + 1], in.px[k + 2]);
        int ye = lut[y];
        double gain = ye / std::max(1.0, static_cast<double>(y));
        for (int c = 0; c < 3; ++c) out.px[k + c] = y == 0 ? static_cast<uint8_t>(ye) : clampByte(in.px[k + c] * gain);
    }
    if (histOut) *histOut = h;
}

// ---------------------------------------------------------------------------
// Noise and quality
// ---------------------------------------------------------------------------
namespace {
struct Lcg {
    uint32_t s;
    double next() { s = s * 1664525u + 1013904223u; return ((s >> 8) + 0.5) / 16777216.0; }   // (0, 1)
};
}

void addGaussianNoise(Image& img, double sigma, uint32_t seed, bool grey) {
    Lcg r{ seed };
    auto gauss = [&r]() { return std::sqrt(-2.0 * std::log(r.next())) * std::cos(6.283185307179586 * r.next()); };
    for (size_t k = 0; k < img.px.size(); k += 3) {
        double z = gauss();
        for (int c = 0; c < 3; ++c) img.px[k + c] = clampByte(img.px[k + c] + sigma * (grey || c == 0 ? z : gauss()));
    }
}

void addSaltPepper(Image& img, double probability, uint32_t seed) {
    Lcg r{ seed };
    for (size_t k = 0; k < img.px.size(); k += 3) {
        double u = r.next();
        if (u < probability * 0.5) img.px[k] = img.px[k + 1] = img.px[k + 2] = 0;
        else if (u < probability) img.px[k] = img.px[k + 1] = img.px[k + 2] = 255;
    }
}

double mse(const Image& a, const Image& b) {
    if (a.px.size() != b.px.size() || a.px.empty()) return 0.0;
    double s = 0.0;
    for (size_t i = 0; i < a.px.size(); ++i) {
        double d = static_cast<double>(a.px[i]) - b.px[i];
        s += d * d;
    }
    return s / a.px.size();
}

double psnr(const Image& a, const Image& b) {
    double m = mse(a, b);
    return m < 1e-12 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / m);
}

// ---------------------------------------------------------------------------
// Binary morphology + connected components
// ---------------------------------------------------------------------------
Mask erode(const Mask& in) {
    Mask out(in.w, in.h);
    for (int y = 0; y < in.h; ++y)
        for (int x = 0; x < in.w; ++x) {
            uint8_t keep = 1;
            for (int j = -1; j <= 1 && keep; ++j)
                for (int i = -1; i <= 1; ++i)
                    if (!in.at(x + i, y + j)) { keep = 0; break; }
            out.m[static_cast<size_t>(y) * in.w + x] = keep;
        }
    return out;
}

Mask dilate(const Mask& in) {
    Mask out(in.w, in.h);
    for (int y = 0; y < in.h; ++y)
        for (int x = 0; x < in.w; ++x) {
            uint8_t any = 0;
            for (int j = -1; j <= 1 && !any; ++j)
                for (int i = -1; i <= 1; ++i)
                    if (in.at(x + i, y + j)) { any = 1; break; }
            out.m[static_cast<size_t>(y) * in.w + x] = any;
        }
    return out;
}

std::vector<Blob> connectedComponents(const Mask& mask, int minArea) {
    const int W = mask.w, H = mask.h;
    std::vector<int> label(static_cast<size_t>(W) * H, 0);
    std::vector<int> parent(1, 0);
    auto find = [&parent](int a) {
        while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; }
        return a;
    };
    // pass 1: provisional labels from the already-visited neighbours (W, NW, N, NE)
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (!mask.at(x, y)) continue;
            int best = 0;
            const int nx[4] = { x - 1, x - 1, x, x + 1 }, ny[4] = { y, y - 1, y - 1, y - 1 };
            for (int n = 0; n < 4; ++n) {
                if (nx[n] < 0 || ny[n] < 0 || nx[n] >= W) continue;
                int l = label[static_cast<size_t>(ny[n]) * W + nx[n]];
                if (!l) continue;
                if (!best) best = l;
                else {
                    int a = find(best), b = find(l);
                    if (a != b) parent[std::max(a, b)] = std::min(a, b);
                }
            }
            if (!best) {
                best = static_cast<int>(parent.size());
                parent.push_back(best);
            }
            label[static_cast<size_t>(y) * W + x] = best;
        }
    // pass 2: resolve equivalences and accumulate bounding boxes
    std::vector<Blob> boxes(parent.size(), Blob{ W, H, -1, -1, 0 });
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            int l = label[static_cast<size_t>(y) * W + x];
            if (!l) continue;
            Blob& b = boxes[find(l)];
            b.x0 = std::min(b.x0, x); b.y0 = std::min(b.y0, y);
            b.x1 = std::max(b.x1, x); b.y1 = std::max(b.y1, y);
            b.area++;
        }
    std::vector<Blob> result;
    for (const Blob& b : boxes)
        if (b.area >= minArea) result.push_back(b);
    std::sort(result.begin(), result.end(), [](const Blob& a, const Blob& b) { return a.area > b.area; });
    return result;
}

} // namespace imgops

namespace imgops {

// ---------------------------------------------------------------------------
// Otsu
// ---------------------------------------------------------------------------
int otsuThreshold(const Histogram& h, std::array<double, 256>* sigmaB) {
    const double N = std::max(1, h.total);
    double sumAll = 0.0;
    for (int v = 0; v < 256; ++v) sumAll += static_cast<double>(v) * h.count[v];
    double w0 = 0.0, sum0 = 0.0, best = -1.0;
    int bestT = 0;
    for (int t = 0; t < 256; ++t) {
        w0 += h.count[t];
        sum0 += static_cast<double>(t) * h.count[t];
        double w1 = N - w0, s = 0.0;
        if (w0 > 0.0 && w1 > 0.0) {
            double mu0 = sum0 / w0, mu1 = (sumAll - sum0) / w1;
            s = (w0 / N) * (w1 / N) * (mu0 - mu1) * (mu0 - mu1);
        }
        if (sigmaB) (*sigmaB)[t] = s;
        if (s > best) { best = s; bestT = t; }
    }
    return bestT;
}

void threshold(const Image& in, int t, Image& out) {
    out = Image(in.w, in.h);
    for (int y = 0; y < in.h; ++y)
        for (int x = 0; x < in.w; ++x) {
            int v = in.at(x, y, 3) > t ? 255 : 0;
            out.set(x, y, v, v, v);
        }
}

// ---------------------------------------------------------------------------
// Frequency domain
// ---------------------------------------------------------------------------
float filterResponse(FreqFilter type, float D, float c) {
    c = std::max(c, 1e-3f);
    switch (type) {
        case FreqFilter::IdealLow:     return D <= c ? 1.0f : 0.0f;
        case FreqFilter::GaussianLow:  return std::exp(-D * D / (2.0f * c * c));
        case FreqFilter::IdealHigh:    return D <= c ? 0.0f : 1.0f;
        case FreqFilter::GaussianHigh: return 1.0f - std::exp(-D * D / (2.0f * c * c));
    }
    return 1.0f;
}

namespace {
using cf = std::complex<float>;

// 1D DFT of n samples with stride; sign -1 forward, +1 inverse (unscaled)
void dft1(const cf* in, cf* out, int n, int stride, float sign, const std::vector<cf>& twiddle) {
    for (int k = 0; k < n; ++k) {
        cf s(0.0f, 0.0f);
        for (int x = 0; x < n; ++x) {
            cf t = twiddle[(static_cast<size_t>(k) * x) % n];
            s += in[x * stride] * (sign < 0 ? t : std::conj(t));
        }
        out[k * stride] = s;
    }
}

void dft2(std::vector<cf>& a, int w, int h, float sign) {
    std::vector<cf> tw(w), th(h), tmp(std::max(w, h)), res(std::max(w, h));
    for (int i = 0; i < w; ++i) tw[i] = std::polar(1.0f, -6.2831853f * i / w);
    for (int i = 0; i < h; ++i) th[i] = std::polar(1.0f, -6.2831853f * i / h);
    for (int y = 0; y < h; ++y) {                                          // rows
        dft1(&a[static_cast<size_t>(y) * w], res.data(), w, 1, sign, tw);
        std::copy(res.begin(), res.begin() + w, a.begin() + static_cast<size_t>(y) * w);
    }
    for (int x = 0; x < w; ++x) {                                          // columns
        for (int y = 0; y < h; ++y) tmp[y] = a[static_cast<size_t>(y) * w + x];
        dft1(tmp.data(), res.data(), h, 1, sign, th);
        for (int y = 0; y < h; ++y) a[static_cast<size_t>(y) * w + x] = res[y];
    }
}
}

void frequencyFilter(const Image& in, FreqFilter type, float cutoff, Image& out, bool grey, FreqResult* info) {
    const int w = in.w, h = in.h;
    const size_t N = static_cast<size_t>(w) * h;
    out = Image(w, h);
    // H(u,v) with the zero frequency at index (0,0) after shifting: D measured to the nearest copy of the origin
    std::vector<float> H(N);
    for (int v = 0; v < h; ++v)
        for (int u = 0; u < w; ++u) {
            float du = static_cast<float>(std::min(u, w - u)), dv = static_cast<float>(std::min(v, h - v));
            H[static_cast<size_t>(v) * w + u] = filterResponse(type, std::sqrt(du * du + dv * dv), cutoff);
        }
    const bool high = type == FreqFilter::IdealHigh || type == FreqFilter::GaussianHigh;
    std::vector<cf> a(N);
    const int channels = grey ? 1 : 3;
    for (int c = 0; c < channels; ++c) {
        int ch = grey ? 3 : c;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) a[static_cast<size_t>(y) * w + x] = cf(static_cast<float>(in.at(x, y, ch)), 0.0f);
        dft2(a, w, h, -1.0f);
        if (info && c == 0) {                                                 // centred log-magnitude for display
            info->logMagnitude.assign(N, 0.0f);
            info->response.assign(N, 0.0f);
            float mx = 1e-6f;
            for (int v = 0; v < h; ++v)
                for (int u = 0; u < w; ++u) {
                    size_t src = static_cast<size_t>(v) * w + u;
                    size_t dst = static_cast<size_t>((v + h / 2) % h) * w + (u + w / 2) % w;
                    float m = std::log(1.0f + std::abs(a[src]));
                    info->logMagnitude[dst] = m;
                    info->response[dst] = H[src];
                    mx = std::max(mx, m);
                }
            for (float& m : info->logMagnitude) m /= mx;
        }
        for (size_t i = 0; i < N; ++i) a[i] *= H[i];                          // G = H . F
        dft2(a, w, h, +1.0f);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                float v = a[static_cast<size_t>(y) * w + x].real() / static_cast<float>(N) + (high ? 128.0f : 0.0f);
                uint8_t b = clampByte(v);
                size_t k = out.index(x, y);
                if (grey) out.px[k] = out.px[k + 1] = out.px[k + 2] = b;
                else out.px[k + c] = b;
            }
    }
}

} // namespace imgops
