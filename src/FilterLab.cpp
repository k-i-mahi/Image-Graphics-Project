#include "FilterLab.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------
struct Preset { const char* name; int size; float w[25]; bool absolute; bool offset128; };

static const Preset PRESETS[] = {
    { "Identity", 3, { 0, 0, 0, 0, 1, 0, 0, 0, 0 }, false, false },
    { "Box blur", 3, { 1, 1, 1, 1, 1, 1, 1, 1, 1 }, false, false },
    { "Gaussian 3x3", 3, { 1, 2, 1, 2, 4, 2, 1, 2, 1 }, false, false },
    { "Gaussian 5x5", 5, { 1, 4, 7, 4, 1, 4, 16, 26, 16, 4, 7, 26, 41, 26, 7, 4, 16, 26, 16, 4, 1, 4, 7, 4, 1 }, false, false },
    { "Sharpen", 3, { 0, -1, 0, -1, 5, -1, 0, -1, 0 }, false, false },
    { "Laplacian", 3, { 0, 1, 0, 1, -4, 1, 0, 1, 0 }, true, false },
    { "Laplacian 8", 3, { -1, -1, -1, -1, 8, -1, -1, -1, -1 }, true, false },
    { "Sobel X", 3, { -1, 0, 1, -2, 0, 2, -1, 0, 1 }, true, false },
    { "Sobel Y", 3, { -1, -2, -1, 0, 0, 0, 1, 2, 1 }, true, false },
    { "Prewitt X", 3, { -1, 0, 1, -1, 0, 1, -1, 0, 1 }, true, false },
    { "Emboss", 3, { -2, -1, 0, -1, 0, 1, 0, 1, 2 }, false, true },
    { "Motion blur", 5, { 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1 }, false, false },
};
static const int PRESET_COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);

static const glm::ivec2 RESOLUTIONS[] = { { 64, 36 }, { 96, 54 }, { 128, 72 }, { 192, 108 }, { 320, 180 } };
static const int RES_COUNT = 5;
static const float SPEEDS[] = { 1, 3, 10, 30, 100, 300, 1000, 3000, 20000 };   // pixels per second
static const int SPEED_COUNT = 9;

static const char* OP_NAMES[OP_COUNT] = { "Convolution", "Median filter", "Min filter (erosion)", "Max filter (dilation)",
                                          "Histogram equalization", "Otsu thresholding", "Frequency-domain filter" };
static const char* FREQ_NAMES[4] = { "Ideal low-pass", "Gaussian low-pass", "Ideal high-pass", "Gaussian high-pass" };
static const char* NOISE_NAMES[NOISE_COUNT] = { "none", "Gaussian", "salt & pepper" };
static const char* CH_NAMES[4] = { "R", "G", "B", "Y" };

// Colours

static std::string num(float v) {
    char b[32];
    if (std::fabs(v - std::round(v)) < 1e-4f) std::snprintf(b, sizeof(b), "%d", static_cast<int>(std::lround(v)));
    else std::snprintf(b, sizeof(b), "%.2f", v);
    return b;
}

static std::string fmt(const char* f, double v) {
    char b[64];
    std::snprintf(b, sizeof(b), f, v);
    return b;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
bool FilterLab::init() {
    if (!imageShader.load("shaders/image.vert", "shaders/image.frag")) return false;
    for (GLuint* t : { &texIn, &texOut, &texSpec }) {
        glGenTextures(1, t);
        glBindTexture(GL_TEXTURE_2D, *t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    loadPreset(presetIndex);
    return true;
}

void FilterLab::open() {
    active = true;
    captureRequested = true;
    editCell = -1;
}

void FilterLab::loadPreset(int index) {
    presetIndex = (index % PRESET_COUNT + PRESET_COUNT) % PRESET_COUNT;
    const Preset& p = PRESETS[presetIndex];
    kernel = LabKernel();
    kernel.size = p.size;
    for (int i = 0; i < p.size * p.size; ++i) kernel.w[i] = p.w[i];
    kernel.absolute = p.absolute;
    kernel.offset128 = p.offset128;
    kernel.autoDiv = true;
    applyAutoDivisor();
    if (op != OP_CONVOLVE) op = OP_CONVOLVE;
    editCell = -1;
    dirty = true;
}

void FilterLab::applyAutoDivisor() {
    if (kernel.autoDiv) kernel.autoDivisor();
}

// Full-resolution RGBA snapshot of the camera image; row 0 = top
void FilterLab::capture(GLuint colorTex, int texW, int texH) {
    std::vector<unsigned char> raw(static_cast<size_t>(texW) * texH * 4);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
    snapW = texW;
    snapH = texH;
    snapshot.resize(raw.size());
    for (int y = 0; y < texH; ++y)
        std::copy_n(&raw[static_cast<size_t>(texH - 1 - y) * texW * 4], texW * 4, &snapshot[static_cast<size_t>(y) * texW * 4]);
    inputDirty = true;
    restartScan();
}

// Box-downsample the snapshot, optional grey conversion, then add the chosen noise
void FilterLab::rebuildInput() {
    inputDirty = false;
    dirty = true;
    imgW = RESOLUTIONS[resIndex].x;
    imgH = RESOLUTIONS[resIndex].y;
    clean = imgops::Image(imgW, imgH);
    if (!snapshot.empty()) {
        for (int y = 0; y < imgH; ++y) {
            int y0 = y * snapH / imgH, y1 = std::max(y0 + 1, (y + 1) * snapH / imgH);
            for (int x = 0; x < imgW; ++x) {
                int x0 = x * snapW / imgW, x1 = std::max(x0 + 1, (x + 1) * snapW / imgW);
                double acc[3] = { 0, 0, 0 };
                int n = 0;
                for (int sy = y0; sy < y1; ++sy)
                    for (int sx = x0; sx < x1; ++sx, ++n)
                        for (int c = 0; c < 3; ++c) acc[c] += snapshot[(static_cast<size_t>(sy) * snapW + sx) * 4 + c];
                clean.set(x, y, static_cast<int>(acc[0] / n + 0.5), static_cast<int>(acc[1] / n + 0.5), static_cast<int>(acc[2] / n + 0.5));
            }
        }
    }
    if (gray)
        for (int y = 0; y < imgH; ++y)
            for (int x = 0; x < imgW; ++x) {
                int l = clean.at(x, y, 3);
                clean.set(x, y, l, l, l);
            }
    input = clean;
    if (noise == NOISE_GAUSSIAN) imgops::addGaussianNoise(input, 25.0, 12345u, gray);   // sigma = 25 grey levels
    else if (noise == NOISE_SALT_PEPPER) imgops::addSaltPepper(input, 0.08, 12345u);   // 8 % of the pixels
    reveal = std::min(reveal, imgW * imgH);
    focus = glm::clamp(focus, glm::ivec2(0), glm::ivec2(imgW - 1, imgH - 1));
}

// ---------------------------------------------------------------------------
// The operations: all maths lives in ImageOps (unit-tested); the lab only
// calls it and shows the intermediate numbers.
// ---------------------------------------------------------------------------
int FilterLab::inAt(int x, int y, int c) const { return input.at(x, y, c); }
int FilterLab::outAt(int x, int y, int c) const { return output.at(x, y, c); }

int FilterLab::convolveAt(int x, int y, int c, float* sumOut) const {
    return imgops::convolvePixel(input, kernel, x, y, c, sumOut);
}

void FilterLab::windowValues(int x, int y, int c, std::vector<int>& vals) const {
    imgops::windowSorted(input, kernel.size, x, y, c, vals);
}

void FilterLab::compute() {
    dirty = false;
    switch (op) {
        case OP_CONVOLVE: imgops::convolve(input, kernel, output, gray); break;
        case OP_MEDIAN:   imgops::rankFilter(input, kernel.size, imgops::Rank::Median, output, gray); break;
        case OP_MIN:      imgops::rankFilter(input, kernel.size, imgops::Rank::Min, output, gray); break;
        case OP_MAX:      imgops::rankFilter(input, kernel.size, imgops::Rank::Max, output, gray); break;
        case OP_HISTEQ:   imgops::equalize(input, output, &hist); break;
        case OP_OTSU:
            hist = imgops::lumaHistogram(input);
            otsuT = imgops::otsuThreshold(hist, &sigmaB);
            imgops::threshold(input, otsuT, output);
            break;
        case OP_FREQ:
            imgops::frequencyFilter(input, freqType, freqCutoff, output, gray, &freq);
            uploadSpectrum();
            break;
        default: break;
    }
    // PSNR = 10 log10(255^2 / MSE) against the clean (noise-free) image
    psnrIn = imgops::psnr(input, clean);
    psnrOut = imgops::psnr(output, clean);
    uploadTextures();
}

// Spectrum image: log |F(u,v)| in grey, the pass band of H(u,v) tinted violet, the stop band darkened
void FilterLab::uploadSpectrum() {
    std::vector<unsigned char> rgb(static_cast<size_t>(imgW) * imgH * 3);
    // Contrast: the DC term dwarfs everything else, so normalise by the strongest non-DC frequency
    size_t dc = static_cast<size_t>(imgH / 2) * imgW + imgW / 2;
    float lo = 1.0f, hi = 0.0f;
    for (size_t i = 0; i < freq.logMagnitude.size(); ++i)
        if (i != dc) { lo = std::min(lo, freq.logMagnitude[i]); hi = std::max(hi, freq.logMagnitude[i]); }
    for (size_t i = 0; i < freq.logMagnitude.size(); ++i) {
        float m = std::clamp((freq.logMagnitude[i] - lo) / std::max(1e-4f, hi - lo), 0.0f, 1.0f), h = freq.response[i];
        m = std::pow(m, 1.3f);
        float k = 0.25f + 0.75f * h;
        rgb[i * 3 + 0] = static_cast<unsigned char>(std::min(255.0f, 255.0f * m * k + 40.0f * h));
        rgb[i * 3 + 1] = static_cast<unsigned char>(std::min(255.0f, 255.0f * m * k * 0.9f + 18.0f * h));
        rgb[i * 3 + 2] = static_cast<unsigned char>(std::min(255.0f, 255.0f * m * k + 70.0f * h));
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, texSpec);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, imgW, imgH, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void FilterLab::uploadTextures() {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, texIn);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, imgW, imgH, 0, GL_RGB, GL_UNSIGNED_BYTE, input.px.data());
    glBindTexture(GL_TEXTURE_2D, texOut);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, imgW, imgH, 0, GL_RGB, GL_UNSIGNED_BYTE, output.px.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

// ---------------------------------------------------------------------------
// Scan, keys, editing
// ---------------------------------------------------------------------------
void FilterLab::restartScan() {
    reveal = 0;
    accum = 0.0f;
    playing = true;
    focus = glm::ivec2(0);
}

void FilterLab::setFocusFromReveal() {
    int idx = std::clamp(reveal - 1, 0, imgW * imgH - 1);
    focus = glm::ivec2(idx % imgW, idx / imgW);
}

void FilterLab::update(float dt) {
    if (!active) return;
    sentFlash = std::max(0.0f, sentFlash - dt);
    if (inputDirty && !snapshot.empty()) rebuildInput();
    if (dirty && !input.empty()) compute();
    const int N = imgW * imgH;
    if (playing && reveal < N) {
        accum += dt * SPEEDS[speedIndex];
        int steps = static_cast<int>(accum);
        accum -= steps;
        if (steps > 0) {
            reveal = std::min(N, reveal + steps);
            setFocusFromReveal();
        }
        if (reveal >= N) playing = false;
    }
}

void FilterLab::commitEdit() {
    if (editCell < 0) return;
    if (!editBuffer.empty() && editBuffer != "-" && editBuffer != ".") {
        float v = static_cast<float>(std::atof(editBuffer.c_str()));
        if (editCell == 100) {
            if (std::fabs(v) > 1e-6f) { kernel.divisor = v; kernel.autoDiv = false; }
        } else {
            kernel.w[editCell] = v;
            applyAutoDivisor();
        }
        dirty = true;
    }
    editCell = -1;
    editBuffer.clear();
}

void FilterLab::onChar(unsigned int c) {
    if (editCell < 0) return;
    if ((c >= '0' && c <= '9') || c == '.' || (c == '-' && editBuffer.empty()))
        if (editBuffer.size() < 8) editBuffer.push_back(static_cast<char>(c));
}

void FilterLab::onKey(int key) {
    const int N = imgW * imgH;
    if (editCell >= 0) {
        if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) commitEdit();
        else if (key == GLFW_KEY_ESCAPE) { editCell = -1; editBuffer.clear(); }
        else if (key == GLFW_KEY_BACKSPACE && !editBuffer.empty()) editBuffer.pop_back();
        else if (key == GLFW_KEY_TAB && editCell < 100) {
            int next = (editCell + 1) % (kernel.size * kernel.size);
            commitEdit();
            editCell = next;
        }
        return;
    }
    switch (key) {
        case GLFW_KEY_ESCAPE: case GLFW_KEY_0: case GLFW_KEY_V: active = false; break;
        case GLFW_KEY_SPACE:
            if (reveal >= N) restartScan(); else playing = !playing;
            break;
        case GLFW_KEY_RIGHT: playing = false; reveal = std::min(N, reveal + 1); setFocusFromReveal(); break;
        case GLFW_KEY_LEFT:  playing = false; reveal = std::max(1, reveal - 1); setFocusFromReveal(); break;
        case GLFW_KEY_UP:    speedIndex = std::min(SPEED_COUNT - 1, speedIndex + 1); break;
        case GLFW_KEY_DOWN:  speedIndex = std::max(0, speedIndex - 1); break;
        case GLFW_KEY_ENTER: reveal = N; playing = false; break;
        case GLFW_KEY_R:     restartScan(); break;
        case GLFW_KEY_W: focus.y = std::max(0, focus.y - 1); playing = false; break;
        case GLFW_KEY_S: focus.y = std::min(imgH - 1, focus.y + 1); playing = false; break;
        case GLFW_KEY_A: focus.x = std::max(0, focus.x - 1); playing = false; break;
        case GLFW_KEY_D: focus.x = std::min(imgW - 1, focus.x + 1); playing = false; break;
        case GLFW_KEY_O: op = static_cast<LabOp>((op + 1) % OP_COUNT); dirty = true; break;
        case GLFW_KEY_P: case GLFW_KEY_K: loadPreset(presetIndex + 1); break;
        case GLFW_KEY_G: gray = !gray; inputDirty = true; break;
        case GLFW_KEY_C: channel = (channel + 1) % 3; break;
        case GLFW_KEY_I: noise = static_cast<LabNoise>((noise + 1) % NOISE_COUNT); inputDirty = true; break;
        case GLFW_KEY_N: captureRequested = true; break;
        case GLFW_KEY_F: freqType = static_cast<imgops::FreqFilter>((static_cast<int>(freqType) + 1) % 4); if (op == OP_FREQ) dirty = true; break;
        case GLFW_KEY_LEFT_BRACKET: freqCutoff = std::max(1.0f, freqCutoff - 1.0f); if (op == OP_FREQ) dirty = true; break;
        case GLFW_KEY_RIGHT_BRACKET: freqCutoff = std::min(40.0f, freqCutoff + 1.0f); if (op == OP_FREQ) dirty = true; break;
        case GLFW_KEY_U:
            if (op == OP_CONVOLVE) { sendToLive = true; sentFlash = 2.5f; }
            break;
        case GLFW_KEY_MINUS: resIndex = std::max(0, resIndex - 1); inputDirty = true; restartScan(); break;
        case GLFW_KEY_EQUAL: resIndex = std::min(RES_COUNT - 1, resIndex + 1); inputDirty = true; restartScan(); break;
        case GLFW_KEY_Z: {
            LabKernel old = kernel;
            int ns = kernel.size == 3 ? 5 : 3;
            std::fill(kernel.w, kernel.w + 25, 0.0f);
            int d = (ns - old.size) / 2;   // +1 when growing, -1 when shrinking
            for (int j = 0; j < ns; ++j)
                for (int i = 0; i < ns; ++i) {
                    int oi = i - d, oj = j - d;
                    if (oi >= 0 && oj >= 0 && oi < old.size && oj < old.size) kernel.w[j * ns + i] = old.w[oj * old.size + oi];
                }
            kernel.size = ns;
            applyAutoDivisor();
            dirty = true;
            break;
        }
        default: break;
    }
}

std::string FilterLab::kernelName() const {
    bool edited = false;
    const Preset& p = PRESETS[presetIndex];
    if (p.size != kernel.size) edited = true;
    for (int i = 0; i < kernel.size * kernel.size && !edited; ++i) edited = kernel.w[i] != p.w[i];
    return std::string(p.name) + (edited ? " (edited)" : "");
}

std::string FilterLab::statusLine() const {
    return std::string(OP_NAMES[op]) + (op == OP_CONVOLVE ? std::string(" - ") + PRESETS[presetIndex].name : "");
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
namespace {
using F = Overlay2D::Font;

// Theme
const glm::vec4 BG_TOP(0.055f, 0.062f, 0.090f, 1.0f), BG_BOTTOM(0.028f, 0.032f, 0.048f, 1.0f);
const glm::vec4 CARD(0.090f, 0.100f, 0.142f, 1.0f), CARD_HEAD(0.112f, 0.124f, 0.172f, 1.0f), INSET(0.060f, 0.067f, 0.095f, 1.0f);
const glm::vec4 TEXT(0.93f, 0.95f, 0.98f, 1.0f), MUTED(0.58f, 0.63f, 0.73f, 1.0f), FAINT(0.36f, 0.40f, 0.50f, 1.0f);
const glm::vec4 AMBER(1.0f, 0.78f, 0.28f, 1.0f), CYANC(0.30f, 0.84f, 1.0f, 1.0f), VIOLET(0.68f, 0.55f, 1.0f, 1.0f);
const glm::vec4 GREEN(0.40f, 0.92f, 0.58f, 1.0f), REDC(1.0f, 0.36f, 0.33f, 1.0f);

glm::vec4 alpha(glm::vec4 c, float a) { c.a = a; return c; }

void centred(Overlay2D& ui, float cx, float cy, const std::string& s, float scale, const glm::vec4& c, F font = F::UI) {
    ui.text(cx - ui.textWidth(s, scale, font) * 0.5f, cy - 3.6f * scale, s, scale, c, font);
}

// Text that shrinks to fit a box width
void fitted(Overlay2D& ui, float cx, float cy, float maxW, const std::string& s, float scale, const glm::vec4& c, F font = F::UI) {
    float w = ui.textWidth(s, 1.0f, font);
    centred(ui, cx, cy, s, std::min(scale, (maxW - 4.0f) / std::max(1.0f, w)), c, font);
}

glm::vec4 grayCol(int v) { float g = v / 255.0f; return glm::vec4(g, g, g, 1.0f); }
glm::vec4 textOn(const glm::vec4& bg) { return (bg.r * 0.3f + bg.g * 0.59f + bg.b * 0.11f) > 0.55f ? glm::vec4(0.05f, 0.05f, 0.07f, 1) : glm::vec4(1); }

void disc(Overlay2D& ui, glm::vec2 c, float r, const glm::vec4& inner, const glm::vec4& outer) {
    const int seg = 32;
    for (int i = 0; i < seg; ++i) {
        float a0 = 6.2831853f * i / seg, a1 = 6.2831853f * (i + 1) / seg;
        ui.triangle(c, c + r * glm::vec2(std::cos(a0), std::sin(a0)), c + r * glm::vec2(std::cos(a1), std::sin(a1)), inner, outer, outer);
    }
}

void ring(Overlay2D& ui, glm::vec2 c, float r, float t, const glm::vec4& col) {
    const int seg = 48;
    for (int i = 0; i < seg; ++i) {
        float a0 = 6.2831853f * i / seg, a1 = 6.2831853f * (i + 1) / seg;
        ui.line(c + r * glm::vec2(std::cos(a0), std::sin(a0)), c + r * glm::vec2(std::cos(a1), std::sin(a1)), t, col);
    }
}

// Light cone from a point onto a rectangle: one triangle per rectangle edge + a core ray + a halo
void beam(Overlay2D& ui, glm::vec2 src, glm::vec4 rect, glm::vec3 col, float strength, float u) {
    glm::vec2 centre(rect.x + rect.z * 0.5f, rect.y + rect.w * 0.5f);
    float halo = std::max(std::max(rect.z, rect.w) * 0.5f, 10.0f * u);
    glm::vec4 hr(centre.x - halo, centre.y - halo, 2 * halo, 2 * halo);
    if (rect.z > 40.0f * u) hr = rect;                         // whole-image targets: cone onto the image itself
    glm::vec2 c[4] = { { hr.x, hr.y }, { hr.x + hr.z, hr.y }, { hr.x + hr.z, hr.y + hr.w }, { hr.x, hr.y + hr.w } };
    glm::vec4 a(col, 0.40f * strength), b(col, 0.08f * strength);
    for (int i = 0; i < 4; ++i) ui.triangle(src, c[i], c[(i + 1) % 4], a, b, b);
    ui.line(src, centre, 2.0f * u, glm::vec4(col, 0.6f * strength));
    if (rect.z <= 40.0f * u) disc(ui, centre, halo, glm::vec4(col, 0.45f * strength), glm::vec4(col, 0.0f));
}

// Card: rounded body, slightly lighter header strip, accent dot + title
void card(Overlay2D& ui, float x, float y, float w, float h, float u, const std::string& title, const glm::vec4& accent) {
    ui.panel(x, y, w, h, 10 * u, CARD);
    ui.roundRect(x, y, w, 26 * u, 10 * u, CARD_HEAD);
    ui.rect(x, y + 16 * u, w, 10 * u, CARD_HEAD);
    ui.rect(x, y + 26 * u, w, 1.0f, alpha(accent, 0.35f));
    disc(ui, { x + 14 * u, y + 13 * u }, 4 * u, accent, accent);
    ui.text(x + 24 * u, y + 7.5f * u, title, 1.35f * u, TEXT, F::BOLD);
}

// Rounded "pill" label; returns its width
float pill(Overlay2D& ui, float x, float y, const std::string& s, float u, const glm::vec4& col) {
    float w = ui.textWidth(s, 1.25f * u) + 18 * u;
    ui.roundRect(x, y, w, 20 * u, 10 * u, alpha(col, 0.16f));
    ui.roundOutline(x, y, w, 20 * u, 10 * u, 1.0f, alpha(col, 0.45f));
    ui.text(x + 9 * u, y + 6 * u, s, 1.25f * u, col);
    return w;
}
}

bool FilterLab::button(Overlay2D& ui, float x, float y, float w, float h, const std::string& label, bool on, float ts) {
    bool hover = mouse.x >= x && mouse.x <= x + w && mouse.y >= y && mouse.y <= y + h;
    float r = std::min(h * 0.5f, 7.0f);
    glm::vec4 bg = on ? glm::vec4(0.27f, 0.36f, 0.78f, 1.0f) : (hover ? glm::vec4(0.19f, 0.21f, 0.29f, 1.0f) : glm::vec4(0.135f, 0.15f, 0.205f, 1.0f));
    ui.roundRect(x, y, w, h, r, bg);
    if (!on) ui.roundOutline(x, y, w, h, r, 1.0f, glm::vec4(1, 1, 1, hover ? 0.16f : 0.07f));
    fitted(ui, x + w * 0.5f, y + h * 0.5f, w - 6.0f, label, ts, on ? glm::vec4(1) : TEXT);
    if (hover && clickPending) { clickPending = false; return true; }
    return false;
}

void FilterLab::drawImage(const GeometryManager& geo, GLuint tex, float x, float y, float w, float h, int W, int H, int revealCount,
                          int texW, int texH) {
    if (texW < 0) { texW = imgW; texH = imgH; }
    glDisable(GL_DEPTH_TEST);
    imageShader.use();
    imageShader.setVec4("uRect", glm::vec4(x, y, w, h));
    imageShader.setVec2("uScreen", glm::vec2(static_cast<float>(W), static_cast<float>(H)));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    imageShader.setInt("uImage", 0);
    glUniform2i(imageShader.loc("uSize"), texW, texH);
    imageShader.setInt("uReveal", revealCount);
    imageShader.setFloat("uPixelOnScreen", w / texW);
    geo.drawQuad();
}

// ---------------------------------------------------------------------------
// The lab screen
// ---------------------------------------------------------------------------
void FilterLab::render(Overlay2D& ui, const GeometryManager& geo, int W, int H, float time) {
    if (input.empty()) {
        ui.begin(W, H);
        ui.gradient(0, 0, static_cast<float>(W), static_cast<float>(H), BG_TOP, BG_BOTTOM);
        centred(ui, W * 0.5f, H * 0.5f, "Capturing the camera image...", 2.0f, TEXT);
        ui.end();
        return;
    }
    const float u = H / 720.0f;
    const float ts = 1.4f * u;                          // body text
    const float m = 14.0f * u;
    const float colW = (W - 4 * m) * 0.355f;
    const float midW = W - 4 * m - 2 * colW;
    const float xL = m, xM = xL + colW + m, xR = xM + midW + m;
    const float yCard = 66.0f * u;                      // top row of cards
    const float pad = 9.0f * u;
    const float imgSW = colW - 2 * pad;                 // image width on screen
    const float ih = imgSW * imgH / static_cast<float>(imgW);
    const float yImg = yCard + 34.0f * u;
    const float card1H = ih + 34.0f * u + pad;
    const float ps = imgSW / imgW;                      // screen pixels per image pixel
    const float xLi = xL + pad, xRi = xR + pad;
    const int N = imgW * imgH;
    const bool rank = op == OP_MEDIAN || op == OP_MIN || op == OP_MAX;
    const bool global = op == OP_FREQ;                  // every output pixel depends on the whole image
    const int K = (op == OP_CONVOLVE || rank) ? kernel.size : 1, r = K / 2;
    const int ch = (op == OP_HISTEQ || op == OP_OTSU) ? (gray ? 0 : 3) : (gray ? 0 : channel);
    const float yRow2 = yCard + card1H + 12.0f * u;
    const float card2H = H - 76.0f * u - yRow2;
    const float row2 = yRow2 + 34.0f * u;               // content top inside row-2 cards
    const float row2H = card2H - 40.0f * u;

    // ---- clicks on the images select the pixel ----
    auto pickPixel = [&](float ox) -> bool {
        if (mouse.x < ox || mouse.x >= ox + imgSW || mouse.y < yImg || mouse.y >= yImg + ih) return false;
        focus = glm::ivec2(std::clamp(static_cast<int>((mouse.x - ox) / ps), 0, imgW - 1),
                           std::clamp(static_cast<int>((mouse.y - yImg) / ps), 0, imgH - 1));
        return true;
    };
    if (clickPending && (pickPixel(xLi) || pickPixel(xRi))) {
        clickPending = false;
        playing = false;
        reveal = std::max(reveal, focus.y * imgW + focus.x + 1);
    }

    // ================= background, header, cards =================
    ui.begin(W, H);
    ui.gradient(0, 0, static_cast<float>(W), static_cast<float>(H), BG_TOP, BG_BOTTOM);
    ui.text(m, 12 * u, "Image Operation Lab", 2.3f * u, TEXT, F::BOLD);
    float px = m + ui.textWidth("Image Operation Lab", 2.3f * u, F::BOLD) + 16 * u;
    px += pill(ui, px, 13 * u, OP_NAMES[op], u, VIOLET) + 6 * u;
    if (op == OP_CONVOLVE) px += pill(ui, px, 13 * u, kernelName(), u, VIOLET) + 6 * u;
    if (op == OP_FREQ) px += pill(ui, px, 13 * u, std::string(FREQ_NAMES[static_cast<int>(freqType)]) + "  D0 = " + num(freqCutoff), u, VIOLET) + 6 * u;
    px += pill(ui, px, 13 * u, std::to_string(imgW) + " × " + std::to_string(imgH) + (gray ? "  grey" : "  RGB"), u, MUTED) + 6 * u;
    pill(ui, px, 13 * u, std::string("noise: ") + NOISE_NAMES[noise], u, noise == NOISE_NONE ? MUTED : AMBER);
    ui.text(m, 40 * u, "Every number on this screen is computed by the unit-tested ImageOps library — the light shows which pixels are read and written.",
            1.15f * u, FAINT);

    // PSNR scorecard (top right)
    {
        float cw = 250 * u, cx = W - m - cw, cy = 8 * u;
        ui.panel(cx, cy, cw, 50 * u, 9 * u, CARD, 0.6f);
        ui.text(cx + 12 * u, cy + 8 * u, "PSNR vs clean image", 1.1f * u, MUTED);
        bool meaningful = op != OP_OTSU;
        std::string a = psnrIn > 98 ? "∞" : fmt("%.1f", psnrIn), b = !meaningful ? "—" : (psnrOut > 98 ? "∞" : fmt("%.1f", psnrOut));
        ui.text(cx + 12 * u, cy + 24 * u, a + " dB", 2.0f * u, AMBER, F::BOLD);
        ui.text(cx + 100 * u, cy + 26 * u, "→", 1.8f * u, MUTED);
        glm::vec4 oc = !meaningful ? MUTED : (psnrOut > psnrIn + 0.05 ? GREEN : (psnrOut < psnrIn - 0.05 ? REDC : CYANC));
        ui.text(cx + 128 * u, cy + 24 * u, b + " dB", 2.0f * u, oc, F::BOLD);
    }

    card(ui, xL, yCard, colW, card1H, u, "Original  ·  input", AMBER);
    card(ui, xM, yCard, midW, card1H, u, "Operation", VIOLET);
    card(ui, xR, yCard, colW, card1H, u, "Processed  ·  output", CYANC);
    card(ui, xL, yRow2, colW, card2H, u, "Input neighbourhood", AMBER);
    card(ui, xM, yRow2, midW, card2H, u, "Computation for this pixel", VIOLET);
    card(ui, xR, yRow2, colW, card2H, u, "Output neighbourhood", CYANC);

    // progress of the pixel-by-pixel scan (output card header)
    {
        float bw = 110 * u, bx = xR + colW - bw - 12 * u, by = yCard + 11 * u;
        ui.roundRect(bx, by, bw, 5 * u, 2.5f * u, INSET);
        ui.roundRect(bx, by, std::max(5 * u, bw * std::min(reveal, N) / static_cast<float>(N)), 5 * u, 2.5f * u, CYANC);
        std::string st = reveal >= N ? "done" : (playing ? num(SPEEDS[speedIndex]) + " px/s" : "paused");
        ui.text(bx - ui.textWidth(st, 1.1f * u) - 8 * u, yCard + 9 * u, st, 1.1f * u, MUTED);
    }
    ui.roundRect(xLi - 2, yImg - 2, imgSW + 4, ih + 4, 4 * u, INSET);
    ui.roundRect(xRi - 2, yImg - 2, imgSW + 4, ih + 4, 4 * u, INSET);
    ui.end();

    drawImage(geo, texIn, xLi, yImg, imgSW, ih, W, H, -1);
    drawImage(geo, texOut, xRi, yImg, imgSW, ih, W, H, reveal);

    // ================= light beams =================
    glm::vec2 orb(xM + midW * 0.5f, yCard + 13.0f * u);
    float pulse = 0.85f + 0.15f * std::sin(time * 5.0f);
    glm::vec4 rectA, rectB;
    if (global) {
        rectA = glm::vec4(xLi, yImg, imgSW, ih);
    } else {
        int fx0 = std::max(0, focus.x - r), fy0 = std::max(0, focus.y - r);
        int fx1 = std::min(imgW - 1, focus.x + r), fy1 = std::min(imgH - 1, focus.y + r);
        rectA = glm::vec4(xLi + fx0 * ps, yImg + fy0 * ps, (fx1 - fx0 + 1) * ps, (fy1 - fy0 + 1) * ps);
    }
    float pb = std::max(ps, 4.0f * u);
    rectB = glm::vec4(xRi + (focus.x + 0.5f) * ps - pb * 0.5f, yImg + (focus.y + 0.5f) * ps - pb * 0.5f, pb, pb);
    ui.begin(W, H);
    beam(ui, orb, rectA, glm::vec3(AMBER), pulse * (global ? 0.5f : 1.0f), u);
    beam(ui, orb, rectB, glm::vec3(CYANC), pulse, u);
    disc(ui, orb, 18.0f * u, glm::vec4(1.0f, 0.95f, 0.75f, 0.9f), glm::vec4(0.8f, 0.6f, 1.0f, 0.0f));
    disc(ui, orb, 6.5f * u, glm::vec4(1.0f), glm::vec4(1.0f, 0.95f, 0.85f, 0.7f));
    ui.end(true);

    ui.begin(W, H);
    if (!global) {
        ui.outline(rectA.x, rectA.y, rectA.z, rectA.w, 2.0f, AMBER);
        ui.outline(xLi + focus.x * ps, yImg + focus.y * ps, ps, ps, 1.0f, REDC);
    }
    ui.outline(rectB.x, rectB.y, rectB.z, rectB.w, 2.0f, CYANC);
    ui.end();

    // ================= operation card =================
    ui.begin(W, H);
    float headY = yCard + 6 * u;
    if (button(ui, xM + midW - 56 * u, headY, 22 * u, 15 * u, "‹", false, ts * 1.2f)) { op = static_cast<LabOp>((op + OP_COUNT - 1) % OP_COUNT); dirty = true; }
    if (button(ui, xM + midW - 30 * u, headY, 22 * u, 15 * u, "›", false, ts * 1.2f)) { op = static_cast<LabOp>((op + 1) % OP_COUNT); dirty = true; }
    float body = yCard + 36 * u;                       // content area of the operation card
    float bodyBottom = yCard + card1H - pad;

    auto explain = [&](const std::vector<std::string>& lines, float y0, const glm::vec4& col) {
        float y = y0;
        for (const std::string& l : lines) { fitted(ui, xM + midW * 0.5f, y + 4 * u, midW - 20 * u, l, ts, col); y += 15 * u; }
    };
    if (op == OP_HISTEQ) {
        explain({ "A global point operation: each output value depends only on", "its input value v and the histogram of the WHOLE image.", "",
                  "h(v) = number of pixels with value v", "cdf(v) = h(0) + h(1) + … + h(v)", "out = 255 · (cdf(v) − cdf_min) / (N − cdf_min)", "",
                  gray ? "" : "Colour: equalize luminance Y, scale RGB by Y′/Y" }, body + 6 * u, MUTED);
    } else if (op == OP_OTSU) {
        explain({ "Segmentation: split the pixels into dark and bright", "with the threshold t that best separates the two classes.", "",
                  "ω0, ω1 = class weights     μ0, μ1 = class means", "σB²(t) = ω0 · ω1 · (μ0 − μ1)²", "t* = argmax σB²(t)        out = 255 if Y > t* else 0", "",
                  "t* = " + std::to_string(otsuT) }, body + 6 * u, MUTED);
    } else if (op == OP_FREQ) {
        // spectrum with the filter applied, cut-off circle, filter buttons
        float sh = bodyBottom - body - 26 * u, sw = sh * imgW / static_cast<float>(imgH);
        if (sw > midW - 20 * u) { sw = midW - 20 * u; sh = sw * imgH / static_cast<float>(imgW); }
        float sx = xM + (midW - sw) * 0.5f, sy = body;
        ui.end();
        drawImage(geo, texSpec, sx, sy, sw, sh, W, H, -1);
        ui.begin(W, H);
        glm::vec2 c(sx + sw * 0.5f, sy + sh * 0.5f);
        float sp = sw / imgW;
        ring(ui, c, freqCutoff * sp, 1.5f, alpha(VIOLET, 0.95f));
        ui.text(sx + 6 * u, sy + 5 * u, "log |F(u,v)|  centred", 1.1f * u, TEXT);
        // scroll on the spectrum changes the cut-off
        if (mouse.x >= sx && mouse.x < sx + sw && mouse.y >= sy && mouse.y < sy + sh && scrollPending != 0.0f) {
            freqCutoff = std::clamp(freqCutoff + (scrollPending > 0 ? 1.0f : -1.0f), 1.0f, 40.0f);
            dirty = true;
        }
        float bw = (midW - 20 * u - 3 * 4 * u) / 4.0f, by = bodyBottom - 20 * u;
        const char* shortNames[4] = { "ideal LP", "Gauss LP", "ideal HP", "Gauss HP" };
        for (int i = 0; i < 4; ++i)
            if (button(ui, xM + 10 * u + i * (bw + 4 * u), by, bw, 18 * u, shortNames[i], static_cast<int>(freqType) == i, ts * 0.9f)) {
                freqType = static_cast<imgops::FreqFilter>(i);
                dirty = true;
            }
    } else {
        float kc = std::min(30.0f * u, (midW * 0.5f) / K);
        float gx = xM + (midW - K * kc) * 0.5f - (op == OP_CONVOLVE ? 30 * u : 0.0f);
        float gridTop = body;
        for (int j = 0; j < K; ++j)
            for (int i = 0; i < K; ++i) {
                int idx = j * K + i;
                float cx = gx + i * kc, cy = gridTop + j * kc;
                bool hover = mouse.x >= cx && mouse.x < cx + kc && mouse.y >= cy && mouse.y < cy + kc;
                if (op == OP_CONVOLVE && hover) {
                    if (scrollPending != 0.0f) {
                        kernel.w[idx] += scrollPending > 0 ? 1.0f : -1.0f;
                        applyAutoDivisor();
                        dirty = true;
                    }
                    if (clickPending) { clickPending = false; commitEdit(); editCell = idx; editBuffer.clear(); }
                }
                bool editing = editCell == idx;
                float w = kernel.w[idx];
                glm::vec4 bg = op != OP_CONVOLVE ? glm::vec4(0.20f, 0.20f, 0.32f, 1.0f)
                             : (w > 0 ? glm::vec4(0.14f, 0.24f + std::min(0.35f, w * 0.04f), 0.48f, 1.0f)
                                      : (w < 0 ? glm::vec4(0.46f, 0.15f, 0.20f, 1.0f) : glm::vec4(0.15f, 0.16f, 0.22f, 1.0f)));
                if (hover) bg += glm::vec4(0.07f, 0.07f, 0.07f, 0.0f);
                ui.roundRect(cx + 1.5f, cy + 1.5f, kc - 3, kc - 3, 4 * u, bg);
                if (i == r && j == r) ui.roundOutline(cx + 1.5f, cy + 1.5f, kc - 3, kc - 3, 4 * u, 1.5f, REDC);
                if (editing) ui.roundOutline(cx + 1.5f, cy + 1.5f, kc - 3, kc - 3, 4 * u, 2.0f, AMBER);
                std::string label = op != OP_CONVOLVE ? "•" : (editing ? editBuffer + (std::fmod(time, 1.0f) < 0.5f ? "|" : " ") : num(w));
                fitted(ui, cx + kc * 0.5f, cy + kc * 0.5f, kc - 4, label, ts * 1.25f, TEXT, F::MONO);
            }
        if (op == OP_CONVOLVE) {
            float dx = gx + K * kc + 12 * u, dy = gridTop + (K * kc) * 0.5f - 24 * u;
            ui.text(dx, dy, "divide by", 1.1f * u, MUTED);
            float bw = 52 * u, bh = 22 * u;
            bool hov = mouse.x >= dx && mouse.x < dx + bw && mouse.y >= dy + 12 * u && mouse.y < dy + 12 * u + bh;
            if (hov && clickPending) { clickPending = false; commitEdit(); editCell = 100; editBuffer.clear(); }
            ui.roundRect(dx, dy + 12 * u, bw, bh, 5 * u, editCell == 100 ? glm::vec4(0.32f, 0.26f, 0.10f, 1.0f) : INSET);
            fitted(ui, dx + bw * 0.5f, dy + 12 * u + bh * 0.5f, bw, editCell == 100 ? editBuffer + "|" : num(kernel.divisor), ts * 1.3f, AMBER, F::MONO);
            if (button(ui, dx, dy + 38 * u, bw, 15 * u, "auto", kernel.autoDiv, ts * 0.85f)) { kernel.autoDiv = !kernel.autoDiv; applyAutoDivisor(); dirty = true; }

            // presets
            float py = gridTop + K * kc + 8 * u;
            int perRow = 4;
            float bw2 = (midW - 20 * u - (perRow - 1) * 4 * u) / perRow, bh2 = 16 * u;
            for (int i = 0; i < PRESET_COUNT; ++i) {
                float bx = xM + 10 * u + (i % perRow) * (bw2 + 4 * u), by = py + (i / perRow) * (bh2 + 4 * u);
                if (by + bh2 > bodyBottom) break;
                if (button(ui, bx, by, bw2, bh2, PRESETS[i].name, i == presetIndex, ts * 0.82f)) loadPreset(i);
            }
        } else {
            explain({ "Rank filter: the K×K window values are SORTED, not weighted" }, gridTop + K * kc + 10 * u, MUTED);
        }
    }
    scrollPending = 0.0f;

    // ================= row 2: zoomed neighbourhoods =================
    const int Np = K + 4, half = Np / 2;
    float cell = std::min((colW - 2 * pad) / Np, row2H / Np);
    auto patch = [&](float ox, bool isOut) {
        float px0 = ox + (colW - Np * cell) * 0.5f, py0 = row2;
        for (int j = 0; j < Np; ++j)
            for (int i = 0; i < Np; ++i) {
                int x = focus.x - half + i, y = focus.y - half + j;
                bool outside = x < 0 || y < 0 || x >= imgW || y >= imgH;
                int cxp = std::clamp(x, 0, imgW - 1), cyp = std::clamp(y, 0, imgH - 1);
                bool shown = !isOut || (cyp * imgW + cxp) < reveal;
                const imgops::Image& img = isOut ? output : input;
                size_t k = img.index(cxp, cyp);
                glm::vec4 col = gray ? grayCol(img.px[k]) : glm::vec4(img.px[k] / 255.0f, img.px[k + 1] / 255.0f, img.px[k + 2] / 255.0f, 1.0f);
                if (!shown) col = INSET;
                if (outside) col *= glm::vec4(0.45f, 0.45f, 0.45f, 1.0f);
                float cx = px0 + i * cell, cy = py0 + j * cell;
                ui.roundRect(cx + 1.5f, cy + 1.5f, cell - 3, cell - 3, 3 * u, col);
                std::string label = shown ? std::to_string(img.at(cxp, cyp, ch)) : "?";
                fitted(ui, cx + cell * 0.5f, cy + cell * 0.5f, cell - 4, label, ts * 1.15f, shown ? textOn(col) : FAINT, F::MONO);
            }
        float kx = px0 + (half - r) * cell, ky = py0 + (half - r) * cell;
        if (!isOut && !global) ui.roundOutline(kx, ky, K * cell, K * cell, 4 * u, 2.5f, AMBER);
        ui.roundOutline(px0 + half * cell + 1, py0 + half * cell + 1, cell - 2, cell - 2, 3 * u, 2.0f, isOut ? CYANC : REDC);
        return glm::vec4(kx, ky, K * cell, K * cell);
    };
    {
        std::string at = "(" + std::to_string(focus.x) + ", " + std::to_string(focus.y) + ")";
        std::string lab = at + (ch == 3 ? "  ·  Y" : (gray ? "" : std::string("  ·  ") + CH_NAMES[ch]));
        ui.text(xL + colW - ui.textWidth(lab, 1.15f * u) - 12 * u, yRow2 + 9 * u, lab, 1.15f * u, MUTED);
        ui.text(xR + colW - ui.textWidth(at, 1.15f * u) - 12 * u, yRow2 + 9 * u, at, 1.15f * u, MUTED);
    }
    glm::vec4 zoomIn = patch(xL, false);
    patch(xR, true);
    ui.end();

    // magnifier lines: image footprint -> zoomed window
    ui.begin(W, H);
    if (!global) {
        glm::vec4 lc(1.0f, 0.82f, 0.35f, 0.22f);
        ui.line({ rectA.x, rectA.y + rectA.w }, { zoomIn.x, zoomIn.y }, 1.5f, lc);
        ui.line({ rectA.x + rectA.z, rectA.y + rectA.w }, { zoomIn.x + zoomIn.z, zoomIn.y }, 1.5f, lc);
    }
    float pxo = xR + (colW - Np * cell) * 0.5f + half * cell, pyo = row2 + half * cell;
    glm::vec4 lc2(0.3f, 0.85f, 1.0f, 0.22f);
    ui.line({ rectB.x, rectB.y + rectB.w }, { pxo, pyo }, 1.5f, lc2);
    ui.line({ rectB.x + rectB.z, rectB.y + rectB.w }, { pxo + cell, pyo }, 1.5f, lc2);
    ui.end(true);

    // ================= computation card =================
    ui.begin(W, H);
    float mx = xM + 14 * u;
    if (!gray && op != OP_HISTEQ && op != OP_OTSU) {
        std::string chs = std::string("channel ") + CH_NAMES[ch];
        ui.text(xM + midW - ui.textWidth(chs, 1.15f * u) - 12 * u, yRow2 + 9 * u, chs, 1.15f * u, MUTED);
    }
    int result = outAt(focus.x, focus.y, ch);
    auto resultLine = [&](float y, const std::string& lhs) {
        ui.roundRect(mx - 6 * u, y - 5 * u, midW - 16 * u, 24 * u, 6 * u, alpha(CYANC, 0.10f));
        ui.text(mx, y + 1 * u, lhs, ts * 1.1f, TEXT);
        std::string rv = std::to_string(result);
        ui.text(xM + midW - ui.textWidth(rv, ts * 1.6f, F::BOLD) - 24 * u, y - 1 * u, rv, ts * 1.6f, CYANC, F::BOLD);
    };
    if (op == OP_CONVOLVE) {
        float pc = std::min((midW - 28 * u) / K, (row2H * 0.56f) / K);
        float gx = xM + (midW - K * pc) * 0.5f, gy = row2;
        for (int j = 0; j < K; ++j)
            for (int i = 0; i < K; ++i) {
                float w = kernel.w[j * K + i];
                int v = inAt(focus.x + i - r, focus.y + j - r, ch);
                float prod = w * v;
                glm::vec4 bg = prod > 0 ? glm::vec4(0.13f, 0.21f, 0.38f, 1.0f) : (prod < 0 ? glm::vec4(0.38f, 0.13f, 0.17f, 1.0f) : glm::vec4(0.13f, 0.14f, 0.19f, 1.0f));
                ui.roundRect(gx + i * pc + 1.5f, gy + j * pc + 1.5f, pc - 3, pc - 3, 4 * u, bg);
                fitted(ui, gx + i * pc + pc * 0.5f, gy + j * pc + pc * 0.32f, pc - 4, num(w) + "×" + std::to_string(v), ts * 0.78f, MUTED, F::MONO);
                fitted(ui, gx + i * pc + pc * 0.5f, gy + j * pc + pc * 0.68f, pc - 4, num(prod), ts * 1.05f, TEXT, F::MONO);
            }
        float sum = 0.0f;
        convolveAt(focus.x, focus.y, ch, &sum);
        float v = sum / kernel.divisor;
        float ty = gy + K * pc + 8 * u;
        ui.text(mx, ty, "Σ w·v  =  " + num(sum), ts * 1.05f, TEXT, F::MONO);
        ui.text(mx, ty + 15 * u, "÷ " + num(kernel.divisor) + "  =  " + fmt("%.2f", v), ts * 1.05f, TEXT, F::MONO);
        float yy = ty + 30 * u;
        if (kernel.absolute) { v = std::fabs(v); ui.text(mx, yy, "|x|  =  " + fmt("%.2f", v), ts * 1.05f, TEXT, F::MONO); yy += 15 * u; }
        if (kernel.offset128) { v += 128.0f; ui.text(mx, yy, "+ 128  =  " + fmt("%.2f", v), ts * 1.05f, TEXT, F::MONO); yy += 15 * u; }
        resultLine(std::min(yy + 8 * u, yRow2 + card2H - 30 * u), "round, clamp to 0…255  →");
    } else if (rank) {
        std::vector<int> vals;
        windowValues(focus.x, focus.y, ch, vals);
        int pick = op == OP_MEDIAN ? static_cast<int>(vals.size()) / 2 : (op == OP_MIN ? 0 : static_cast<int>(vals.size()) - 1);
        int perRow = std::min(9, static_cast<int>(vals.size()));
        float pc = std::min((midW - 28 * u) / perRow, 32.0f * u);
        float gx = xM + (midW - perRow * pc) * 0.5f, gy = row2 + 16 * u;
        ui.text(mx, row2, "window values, sorted (smallest → largest)", ts, MUTED);
        for (size_t i = 0; i < vals.size(); ++i) {
            float cx = gx + (i % perRow) * pc, cy = gy + (i / perRow) * pc;
            glm::vec4 col = grayCol(vals[i]);
            ui.roundRect(cx + 1.5f, cy + 1.5f, pc - 3, pc - 3, 4 * u, col);
            fitted(ui, cx + pc * 0.5f, cy + pc * 0.5f, pc - 4, std::to_string(vals[i]), ts, textOn(col), F::MONO);
            if (static_cast<int>(i) == pick) ui.roundOutline(cx + 1.5f, cy + 1.5f, pc - 3, pc - 3, 4 * u, 2.5f, CYANC);
        }
        float ty = gy + ((vals.size() + perRow - 1) / perRow) * pc + 12 * u;
        const char* what = op == OP_MEDIAN ? "median = middle value  (#" : (op == OP_MIN ? "minimum = first value  (#" : "maximum = last value  (#");
        ui.text(mx, ty, what + std::to_string(pick + 1) + " of " + std::to_string(vals.size()) + ")", ts, TEXT);
        if (op == OP_MEDIAN) ui.text(mx, ty + 16 * u, "isolated 0 / 255 noise ends up at the ends of the list", ts * 0.9f, MUTED);
        resultLine(std::min(ty + 40 * u, yRow2 + card2H - 30 * u), "output  →");
    } else if (op == OP_FREQ) {
        // H(D) profile + the pipeline
        float cx0 = mx, cy0 = row2 + 2 * u, cw = midW - 28 * u, chh = row2H * 0.42f;
        ui.roundRect(cx0 - 4 * u, cy0 - 4 * u, cw + 8 * u, chh + 8 * u, 5 * u, INSET);
        float maxD = 40.0f;
        for (int i = 1; i <= 120; ++i) {
            float d0 = maxD * (i - 1) / 120.0f, d1 = maxD * i / 120.0f;
            ui.line({ cx0 + cw * (i - 1) / 120.0f, cy0 + chh * (1.0f - imgops::filterResponse(freqType, d0, freqCutoff)) },
                    { cx0 + cw * i / 120.0f, cy0 + chh * (1.0f - imgops::filterResponse(freqType, d1, freqCutoff)) }, 2.0f, VIOLET);
        }
        float xc = cx0 + cw * freqCutoff / maxD;
        ui.line({ xc, cy0 }, { xc, cy0 + chh }, 1.0f, alpha(AMBER, 0.7f));
        ui.text(cx0 + 4 * u, cy0 + 3 * u, "H(D)", 1.1f * u, VIOLET);
        ui.text(xc + 4 * u, cy0 + chh - 12 * u, "D0", 1.1f * u, AMBER);
        float ty = cy0 + chh + 12 * u;
        ui.text(mx, ty, "F(u,v) = Σx Σy f(x,y) · e^(−j2π(ux/M + vy/N))", ts * 0.95f, TEXT, F::MONO);
        ui.text(mx, ty + 15 * u, "G(u,v) = H(u,v) · F(u,v)        g = IDFT(G)", ts * 0.95f, TEXT, F::MONO);
        bool gauss = freqType == imgops::FreqFilter::GaussianLow || freqType == imgops::FreqFilter::GaussianHigh;
        bool high = freqType == imgops::FreqFilter::IdealHigh || freqType == imgops::FreqFilter::GaussianHigh;
        std::string hs = gauss ? (high ? "H = 1 − e^(−D²/2D0²)" : "H = e^(−D²/2D0²)") : (high ? "H = 0 if D ≤ D0, else 1" : "H = 1 if D ≤ D0, else 0");
        ui.text(mx, ty + 30 * u, hs + (high ? "     (+128 to show negatives)" : ""), ts * 0.95f, MUTED, F::MONO);
        resultLine(std::min(ty + 52 * u, yRow2 + card2H - 30 * u), "every output pixel depends on ALL input pixels  →");
    } else {
        // histogram-based (equalization / Otsu)
        const int Ntot = imgW * imgH;
        float cx0 = mx, cy0 = row2 + 2 * u, cw = midW - 28 * u, chh = row2H * 0.5f;
        ui.roundRect(cx0 - 4 * u, cy0 - 4 * u, cw + 8 * u, chh + 8 * u, 5 * u, INSET);
        int hmax = 1;
        for (int v = 0; v < 256; ++v) hmax = std::max(hmax, hist.count[v]);
        for (int v = 0; v < 256; ++v) {
            float bh = chh * hist.count[v] / static_cast<float>(hmax);
            glm::vec4 bc = op == OP_OTSU ? (v > otsuT ? glm::vec4(0.85f, 0.85f, 0.92f, 0.85f) : glm::vec4(0.35f, 0.42f, 0.62f, 0.9f))
                                         : glm::vec4(0.40f, 0.52f, 0.80f, 0.9f);
            ui.rect(cx0 + cw * v / 256.0f, cy0 + chh - bh, std::max(1.0f, cw / 256.0f), bh, bc);
        }
        int vin = inAt(focus.x, focus.y, ch);
        if (op == OP_HISTEQ) {
            for (int v = 1; v < 256; ++v)
                ui.line({ cx0 + cw * (v - 1) / 256.0f, cy0 + chh - chh * hist.cdf[v - 1] / static_cast<float>(Ntot) },
                        { cx0 + cw * v / 256.0f, cy0 + chh - chh * hist.cdf[v] / static_cast<float>(Ntot) }, 2.0f, AMBER);
            float vx = cx0 + cw * (vin + 0.5f) / 256.0f, vy = cy0 + chh - chh * hist.cdf[vin] / static_cast<float>(Ntot);
            ui.line({ vx, cy0 + chh }, { vx, vy }, 1.5f, REDC);
            ui.line({ vx, vy }, { cx0 + cw, vy }, 1.5f, CYANC);
            ui.text(cx0 + 4 * u, cy0 + 3 * u, "h(v)", 1.1f * u, glm::vec4(0.55f, 0.65f, 0.95f, 1.0f));
            ui.text(cx0 + 4 * u, cy0 + 16 * u, "cdf(v)", 1.1f * u, AMBER);
            float ty = cy0 + chh + 12 * u;
            ui.text(mx, ty, "v = " + std::to_string(vin) + "   cdf(v) = " + std::to_string(hist.cdf[vin]) + "   cdf_min = " +
                    std::to_string(hist.cdfMin) + "   N = " + std::to_string(Ntot), ts * 0.95f, TEXT, F::MONO);
            ui.text(mx, ty + 15 * u, "255 · (" + std::to_string(hist.cdf[vin]) + " − " + std::to_string(hist.cdfMin) + ") / (" +
                    std::to_string(Ntot) + " − " + std::to_string(hist.cdfMin) + ")", ts * 0.95f, MUTED, F::MONO);
            resultLine(std::min(ty + 38 * u, yRow2 + card2H - 30 * u), "out  →");
        } else {
            double smax = 1e-9;
            for (double sv : sigmaB) smax = std::max(smax, sv);
            for (int v = 1; v < 256; ++v)
                ui.line({ cx0 + cw * (v - 1) / 256.0f, cy0 + chh - chh * static_cast<float>(sigmaB[v - 1] / smax) },
                        { cx0 + cw * v / 256.0f, cy0 + chh - chh * static_cast<float>(sigmaB[v] / smax) }, 2.0f, VIOLET);
            float tx = cx0 + cw * (otsuT + 0.5f) / 256.0f;
            ui.line({ tx, cy0 }, { tx, cy0 + chh }, 2.0f, AMBER);
            ui.text(tx + 4 * u, cy0 + 3 * u, "t* = " + std::to_string(otsuT), 1.15f * u, AMBER, F::BOLD);
            ui.text(cx0 + 4 * u, cy0 + 3 * u, "σB²(t)", 1.1f * u, VIOLET);
            float vx = cx0 + cw * (vin + 0.5f) / 256.0f;
            ui.line({ vx, cy0 + chh - 10 * u }, { vx, cy0 + chh }, 3.0f, REDC);
            float ty = cy0 + chh + 12 * u;
            ui.text(mx, ty, "Y = " + std::to_string(vin) + (vin > otsuT ? "  >  " : "  ≤  ") + "t* = " + std::to_string(otsuT) +
                    "   →  class " + (vin > otsuT ? "1 (bright)" : "0 (dark)"), ts * 0.95f, TEXT, F::MONO);
            resultLine(std::min(ty + 24 * u, yRow2 + card2H - 30 * u), "out  →");
        }
    }

    // ================= bottom toolbar =================
    float tbY = H - 66 * u, tbH = 30 * u;
    ui.panel(m, tbY, W - 2 * m, tbH, 8 * u, CARD, 0.6f);
    float by = tbY + 6 * u, bh = 18 * u, bx = m + 8 * u;
    auto tog = [&](const std::string& label, bool on, float w) { bool hit = button(ui, bx, by, w * u, bh, label, on, ts * 0.92f); bx += w * u + 5 * u; return hit; };
    auto sep = [&]() { ui.rect(bx + 1 * u, by + 2 * u, 1.0f, bh - 4 * u, alpha(MUTED, 0.3f)); bx += 8 * u; };
    if (op == OP_CONVOLVE || rank) {
        if (tog(kernel.size == 3 ? "3 × 3" : "5 × 5", false, 50)) onKey(GLFW_KEY_Z);
    }
    if (op == OP_CONVOLVE) {
        if (tog("abs( )", kernel.absolute, 50)) { kernel.absolute = !kernel.absolute; dirty = true; }
        if (tog("+128", kernel.offset128, 44)) { kernel.offset128 = !kernel.offset128; dirty = true; }
    }
    if (op == OP_FREQ) {
        if (tog("D0 −", false, 44)) onKey(GLFW_KEY_LEFT_BRACKET);
        if (tog("D0 +", false, 44)) onKey(GLFW_KEY_RIGHT_BRACKET);
    }
    sep();
    if (tog(gray ? "grey" : "RGB", gray, 46)) onKey(GLFW_KEY_G);
    if (!gray && tog(std::string("show ") + CH_NAMES[channel], false, 62)) onKey(GLFW_KEY_C);
    if (tog(std::string("noise: ") + NOISE_NAMES[noise], noise != NOISE_NONE, 128)) onKey(GLFW_KEY_I);
    if (tog(std::to_string(imgW) + " × " + std::to_string(imgH), false, 72)) { resIndex = (resIndex + 1) % RES_COUNT; inputDirty = true; restartScan(); }
    if (tog("new snapshot", false, 96)) captureRequested = true;
    sep();
    if (tog(playing ? "‖  pause" : (reveal >= N ? "↻  replay" : "▶  play"), playing, 74)) onKey(GLFW_KEY_SPACE);
    if (tog("slower", false, 54)) onKey(GLFW_KEY_DOWN);
    if (tog("faster", false, 54)) onKey(GLFW_KEY_UP);
    if (tog("finish", false, 54)) onKey(GLFW_KEY_ENTER);
    if (op == OP_CONVOLVE) {
        sep();
        if (tog("use in live CCTV  (U)", sentFlash > 0.0f, 140)) onKey(GLFW_KEY_U);
    }
    if (sentFlash > 0.0f) {
        std::string msg = "Kernel sent to the live CCTV pipeline — press Esc, then 4 or 7";
        float mw = ui.textWidth(msg, ts * 1.1f);
        ui.panel(W * 0.5f - mw * 0.5f - 14 * u, tbY - 34 * u, mw + 28 * u, 26 * u, 8 * u, glm::vec4(0.08f, 0.32f, 0.16f, 0.97f));
        ui.text(W * 0.5f - mw * 0.5f, tbY - 26 * u, msg, ts * 1.1f, glm::vec4(0.75f, 1.0f, 0.8f, 1.0f));
    }
    ui.text(m, H - 30 * u, "Mouse: click a kernel cell and type (Enter / Tab) · wheel on a cell ±1 · click any pixel to inspect it"
            "  ·  wheel on the spectrum changes D0", 1.1f * u, FAINT);
    ui.text(m, H - 16 * u, "Keys: Space play · ←/→ step · ↑/↓ speed · WASD move pixel · O operation · P preset · Z size · G grey · C channel"
            " · I noise · F filter · [ ] D0 · −/= resolution · N snapshot · U use live · Esc back", 1.1f * u, FAINT);
    ui.end();
    clickPending = false;
}
