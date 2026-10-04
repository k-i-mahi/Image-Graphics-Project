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

static const char* OP_NAMES[OP_COUNT] = { "CONVOLUTION", "MEDIAN FILTER", "MIN FILTER (erosion)", "MAX FILTER (dilation)", "HISTOGRAM EQUALIZATION" };
static const char* NOISE_NAMES[NOISE_COUNT] = { "none", "Gaussian", "salt & pepper" };
static const char* CH_NAMES[4] = { "R", "G", "B", "Y" };

// Colours
static const glm::vec4 BG(0.050f, 0.056f, 0.080f, 1.0f), PANEL(0.085f, 0.095f, 0.135f, 1.0f);
static const glm::vec4 WHITE(1.0f), DIM(0.62f, 0.67f, 0.76f, 1.0f);
static const glm::vec4 YELLOW(1.0f, 0.82f, 0.22f, 1.0f), CYAN(0.25f, 0.85f, 1.0f, 1.0f), RED(1.0f, 0.28f, 0.25f, 1.0f);

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
    for (GLuint* t : { &texIn, &texOut }) {
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
        default: break;
    }
    // PSNR = 10 log10(255^2 / MSE) against the clean (noise-free) image
    psnrIn = imgops::psnr(input, clean);
    psnrOut = imgops::psnr(output, clean);
    uploadTextures();
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
static void centred(Overlay2D& ui, float cx, float cy, const std::string& s, float scale, const glm::vec4& c) {
    ui.text(cx - ui.textWidth(s, scale) * 0.5f, cy - 3.5f * scale, s, scale, c);
}

// Text that shrinks to fit a box width
static void fitted(Overlay2D& ui, float cx, float cy, float maxW, const std::string& s, float scale, const glm::vec4& c) {
    float w = ui.textWidth(s, 1.0f);
    float sc = std::min(scale, (maxW - 4.0f) / std::max(1.0f, w));
    centred(ui, cx, cy, s, sc, c);
}

static glm::vec4 grayCol(int v) { float g = v / 255.0f; return glm::vec4(g, g, g, 1.0f); }
static glm::vec4 textOn(const glm::vec4& bg) { return (bg.r * 0.3f + bg.g * 0.59f + bg.b * 0.11f) > 0.55f ? glm::vec4(0, 0, 0, 1) : glm::vec4(1); }

static void disc(Overlay2D& ui, glm::vec2 c, float r, const glm::vec4& inner, const glm::vec4& outer) {
    const int seg = 28;
    for (int i = 0; i < seg; ++i) {
        float a0 = 6.2831853f * i / seg, a1 = 6.2831853f * (i + 1) / seg;
        ui.triangle(c, c + r * glm::vec2(std::cos(a0), std::sin(a0)), c + r * glm::vec2(std::cos(a1), std::sin(a1)), inner, outer, outer);
    }
}

// Light cone from a point onto a rectangle: one triangle per rectangle edge
static void beam(Overlay2D& ui, glm::vec2 src, glm::vec4 rect, glm::vec3 col, float strength, float u) {
    // widened cone so even a 1-pixel target gets a visible beam
    glm::vec2 centre(rect.x + rect.z * 0.5f, rect.y + rect.w * 0.5f);
    float halo = std::max(rect.z, 10.0f * u);
    glm::vec4 hr(centre.x - halo, centre.y - halo, 2 * halo, 2 * halo);
    glm::vec2 c[4] = { { hr.x, hr.y }, { hr.x + hr.z, hr.y }, { hr.x + hr.z, hr.y + hr.w }, { hr.x, hr.y + hr.w } };
    glm::vec4 a(col, 0.42f * strength), b(col, 0.10f * strength);
    for (int i = 0; i < 4; ++i) ui.triangle(src, c[i], c[(i + 1) % 4], a, b, b);
    ui.line(src, centre, 2.0f * u, glm::vec4(col, 0.65f * strength));
    // soft round halo where the light lands
    const int seg = 24;
    for (int i = 0; i < seg; ++i) {
        float a0 = 6.2831853f * i / seg, a1 = 6.2831853f * (i + 1) / seg;
        ui.triangle(centre, centre + halo * glm::vec2(std::cos(a0), std::sin(a0)), centre + halo * glm::vec2(std::cos(a1), std::sin(a1)),
                    glm::vec4(col, 0.45f * strength), glm::vec4(col, 0.0f), glm::vec4(col, 0.0f));
    }
}

bool FilterLab::button(Overlay2D& ui, float x, float y, float w, float h, const std::string& label, bool on, float ts) {
    bool hover = mouse.x >= x && mouse.x <= x + w && mouse.y >= y && mouse.y <= y + h;
    glm::vec4 bg = on ? glm::vec4(0.20f, 0.36f, 0.62f, 1.0f) : (hover ? glm::vec4(0.18f, 0.2f, 0.27f, 1.0f) : glm::vec4(0.12f, 0.13f, 0.18f, 1.0f));
    ui.rect(x, y, w, h, bg);
    ui.outline(x, y, w, h, 1.0f, on ? glm::vec4(0.45f, 0.65f, 1.0f, 1.0f) : glm::vec4(0.25f, 0.28f, 0.36f, 1.0f));
    fitted(ui, x + w * 0.5f, y + h * 0.5f, w, label, ts, WHITE);
    if (hover && clickPending) { clickPending = false; return true; }
    return false;
}

void FilterLab::drawImage(const GeometryManager& geo, GLuint tex, float x, float y, float w, float h, int W, int H, int revealCount) {
    glDisable(GL_DEPTH_TEST);
    imageShader.use();
    imageShader.setVec4("uRect", glm::vec4(x, y, w, h));
    imageShader.setVec2("uScreen", glm::vec2(static_cast<float>(W), static_cast<float>(H)));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    imageShader.setInt("uImage", 0);
    glUniform2i(imageShader.loc("uSize"), imgW, imgH);
    imageShader.setInt("uReveal", revealCount);
    imageShader.setFloat("uPixelOnScreen", w / imgW);
    geo.drawQuad();
}

// ---------------------------------------------------------------------------
// The lab screen
// ---------------------------------------------------------------------------
void FilterLab::render(Overlay2D& ui, const GeometryManager& geo, int W, int H, float time) {
    if (input.empty()) {
        ui.begin(W, H);
        ui.rect(0, 0, static_cast<float>(W), static_cast<float>(H), BG);
        centred(ui, W * 0.5f, H * 0.5f, "Capturing the camera image...", 2.0f, WHITE);
        ui.end();
        return;
    }
    const float u = H / 720.0f;
    const float ts = 1.45f * u;                         // body text scale
    const float m = 14.0f * u;
    const float colW = (W - 4 * m) * 0.355f;
    const float midW = W - 4 * m - 2 * colW;
    const float xL = m, xM = xL + colW + m, xR = xM + midW + m;
    const float yImg = 64.0f * u;
    const float ih = colW * imgH / static_cast<float>(imgW);
    const float ps = colW / imgW;                       // screen pixels per image pixel
    const int N = imgW * imgH;
    const bool rank = op == OP_MEDIAN || op == OP_MIN || op == OP_MAX;
    const int K = op == OP_HISTEQ ? 1 : kernel.size, r = K / 2;
    const int ch = op == OP_HISTEQ ? (gray ? 0 : 3) : (gray ? 0 : channel);
    const float row2 = yImg + ih + 34.0f * u;
    const float row2H = H - 70.0f * u - row2;

    // ---- clicks on the images select the pixel ----
    auto pickPixel = [&](float ox) -> bool {
        if (mouse.x < ox || mouse.x >= ox + colW || mouse.y < yImg || mouse.y >= yImg + ih) return false;
        focus = glm::ivec2(std::clamp(static_cast<int>((mouse.x - ox) / ps), 0, imgW - 1),
                           std::clamp(static_cast<int>((mouse.y - yImg) / ps), 0, imgH - 1));
        return true;
    };
    if (clickPending && (pickPixel(xL) || pickPixel(xR))) {
        clickPending = false;
        playing = false;
        reveal = std::max(reveal, focus.y * imgW + focus.x + 1);
    }

    // ================= background, header, frames =================
    ui.begin(W, H);
    ui.rect(0, 0, static_cast<float>(W), static_cast<float>(H), BG);
    ui.text(m, 10 * u, "IMAGE OPERATION LAB", 2.6f * u, YELLOW);
    std::string sub = std::string(OP_NAMES[op]) + (op == OP_CONVOLVE ? "  |  " + std::string(PRESETS[presetIndex].name) : "") +
                      "  |  " + std::to_string(imgW) + " x " + std::to_string(imgH) + " px" + (gray ? "  grey" : "  RGB") +
                      "  |  noise: " + NOISE_NAMES[noise];
    ui.text(m, 34 * u, sub, ts, DIM);
    std::string ps1 = "PSNR vs clean image:  input " + (psnrIn > 98 ? std::string("inf") : fmt("%.1f", psnrIn)) +
                      " dB   ->   output " + (psnrOut > 98 ? std::string("inf") : fmt("%.1f", psnrOut)) + " dB";
    ui.text(W - m - ui.textWidth(ps1, ts), 12 * u, ps1, ts, psnrOut > psnrIn + 0.05 ? glm::vec4(0.4f, 1.0f, 0.5f, 1.0f) : DIM);
    std::string prog = "pixel " + std::to_string(std::min(reveal, N)) + " / " + std::to_string(N) + "   " +
                       (reveal >= N ? "DONE" : (playing ? "PLAYING" : "PAUSED")) + "   " + num(SPEEDS[speedIndex]) + " px/s";
    ui.text(W - m - ui.textWidth(prog, ts), 34 * u, prog, ts, DIM);

    ui.text(xL, yImg - 14 * u, "ORIGINAL  (input)", ts * 1.1f, YELLOW);
    ui.text(xR, yImg - 14 * u, "PROCESSED  (output, built pixel by pixel)", ts * 1.1f, CYAN);
    ui.outline(xL, yImg, colW, ih, 2.0f, glm::vec4(0.3f, 0.32f, 0.4f, 1.0f));
    ui.outline(xR, yImg, colW, ih, 2.0f, glm::vec4(0.3f, 0.32f, 0.4f, 1.0f));
    ui.rect(xM, yImg - 18 * u, midW, ih + 18 * u, PANEL);
    ui.rect(xL, row2 - 16 * u, colW, row2H + 16 * u, PANEL);
    ui.rect(xM, row2 - 16 * u, midW, row2H + 16 * u, PANEL);
    ui.rect(xR, row2 - 16 * u, colW, row2H + 16 * u, PANEL);
    ui.end();

    drawImage(geo, texIn, xL, yImg, colW, ih, W, H, -1);
    drawImage(geo, texOut, xR, yImg, colW, ih, W, H, reveal);

    // ================= light beams =================
    glm::vec2 orb(xM + midW * 0.5f, yImg + 8.0f * u);
    float pulse = 0.85f + 0.15f * std::sin(time * 5.0f);
    int fx0 = std::max(0, focus.x - r), fy0 = std::max(0, focus.y - r);
    int fx1 = std::min(imgW - 1, focus.x + r), fy1 = std::min(imgH - 1, focus.y + r);
    glm::vec4 rectA(xL + fx0 * ps, yImg + fy0 * ps, (fx1 - fx0 + 1) * ps, (fy1 - fy0 + 1) * ps);
    float pb = std::max(ps, 4.0f * u);
    glm::vec4 rectB(xR + (focus.x + 0.5f) * ps - pb * 0.5f, yImg + (focus.y + 0.5f) * ps - pb * 0.5f, pb, pb);
    ui.begin(W, H);
    beam(ui, orb, rectA, glm::vec3(YELLOW), pulse, u);
    beam(ui, orb, rectB, glm::vec3(CYAN), pulse, u);
    disc(ui, orb, 16.0f * u, glm::vec4(1.0f, 0.95f, 0.7f, 0.9f), glm::vec4(1.0f, 0.8f, 0.3f, 0.0f));
    disc(ui, orb, 6.0f * u, glm::vec4(1.0f), glm::vec4(1.0f, 0.95f, 0.8f, 0.6f));
    ui.end(true);

    ui.begin(W, H);
    ui.outline(rectA.x, rectA.y, rectA.z, rectA.w, 2.0f, YELLOW);
    ui.outline(xL + focus.x * ps, yImg + focus.y * ps, ps, ps, 1.0f, RED);
    ui.outline(rectB.x, rectB.y, rectB.z, rectB.w, 2.0f, CYAN);
    ui.end();

    // ================= middle column: operation + kernel =================
    ui.begin(W, H);
    if (button(ui, xM + 6 * u, yImg - 16 * u, 22 * u, 14 * u, "<", false, ts)) { op = static_cast<LabOp>((op + OP_COUNT - 1) % OP_COUNT); dirty = true; }
    if (button(ui, xM + midW - 28 * u, yImg - 16 * u, 22 * u, 14 * u, ">", false, ts)) { op = static_cast<LabOp>((op + 1) % OP_COUNT); dirty = true; }
    fitted(ui, xM + midW * 0.5f, yImg - 9 * u, midW - 64 * u, OP_NAMES[op], ts, WHITE);

    float gridTop = yImg + 28 * u;
    if (op == OP_HISTEQ) {
        float y = gridTop + 6 * u;
        const char* lines[] = { "A GLOBAL point operation: the output", "depends only on the pixel value v and", "the histogram of the WHOLE image.", "",
                                "h(v)   = pixels with value v", "cdf(v) = h(0) + h(1) + ... + h(v)", "out = 255 (cdf(v) - cdf_min)", "            / (N - cdf_min)", "",
                                gray ? "" : "colour: equalize Y, scale RGB by Y'/Y" };
        for (const char* l : lines) { fitted(ui, xM + midW * 0.5f, y + 4 * u, midW - 12 * u, l, ts * 0.95f, DIM); y += 13 * u; }
    } else {
        float kc = std::min(32.0f * u, (midW * 0.62f) / K);
        float gx = xM + (midW - K * kc) * 0.5f - (op == OP_CONVOLVE ? 26 * u : 0.0f);
        // scroll wheel over a cell changes it by 1
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
                glm::vec4 bg = op != OP_CONVOLVE ? glm::vec4(0.22f, 0.25f, 0.35f, 1.0f)
                             : (w > 0 ? glm::vec4(0.12f, 0.22f + std::min(0.4f, w * 0.05f), 0.42f, 1.0f)
                                      : (w < 0 ? glm::vec4(0.42f, 0.13f, 0.13f, 1.0f) : glm::vec4(0.14f, 0.15f, 0.2f, 1.0f)));
                if (hover) bg += glm::vec4(0.08f, 0.08f, 0.08f, 0.0f);
                ui.rect(cx + 1, cy + 1, kc - 2, kc - 2, bg);
                if (i == r && j == r) ui.outline(cx + 1, cy + 1, kc - 2, kc - 2, 1.0f, RED);
                if (editing) ui.outline(cx + 1, cy + 1, kc - 2, kc - 2, 2.0f, YELLOW);
                std::string label = op != OP_CONVOLVE ? "." : (editing ? editBuffer + (std::fmod(time, 1.0f) < 0.5f ? "_" : " ") : num(w));
                fitted(ui, cx + kc * 0.5f, cy + kc * 0.5f, kc, label, ts * 1.2f, WHITE);
            }
        if (op == OP_CONVOLVE) {
            // divisor box
            float dx = gx + K * kc + 10 * u, dy = gridTop + (K * kc) * 0.5f - 22 * u;
            ui.text(dx, dy, "divide by", ts * 0.8f, DIM);
            float bw = 46 * u, bh = 20 * u;
            bool hov = mouse.x >= dx && mouse.x < dx + bw && mouse.y >= dy + 10 * u && mouse.y < dy + 10 * u + bh;
            if (hov && clickPending) { clickPending = false; commitEdit(); editCell = 100; editBuffer.clear(); }
            ui.rect(dx, dy + 10 * u, bw, bh, editCell == 100 ? glm::vec4(0.3f, 0.25f, 0.1f, 1.0f) : glm::vec4(0.16f, 0.17f, 0.24f, 1.0f));
            fitted(ui, dx + bw * 0.5f, dy + 10 * u + bh * 0.5f, bw,
                   editCell == 100 ? editBuffer + "_" : num(kernel.divisor), ts * 1.2f, YELLOW);
            if (button(ui, dx, dy + 34 * u, bw, 14 * u, "auto", kernel.autoDiv, ts * 0.8f)) { kernel.autoDiv = !kernel.autoDiv; applyAutoDivisor(); dirty = true; }
        } else {
            ui.text(xM + 10 * u, gridTop + K * kc + 4 * u, "rank window: values are SORTED, not weighted", ts * 0.8f, DIM);
        }
        // presets (convolution) in rows under the grid
        if (op == OP_CONVOLVE) {
            float py = gridTop + K * kc + 8 * u;
            int perRow = 4;
            float bw = (midW - 12 * u - (perRow - 1) * 4 * u) / perRow, bh = 15 * u;
            for (int i = 0; i < PRESET_COUNT; ++i) {
                float bx = xM + 6 * u + (i % perRow) * (bw + 4 * u), by = py + (i / perRow) * (bh + 3 * u);
                if (by + bh > yImg + ih) break;
                if (button(ui, bx, by, bw, bh, PRESETS[i].name, i == presetIndex, ts * 0.85f)) loadPreset(i);
            }
        }
    }
    scrollPending = 0.0f;

    // ================= row 2: zoomed neighbourhoods =================
    const int Np = K + 4, half = Np / 2;
    float cell = std::min((colW - 12 * u) / Np, (row2H - 10 * u) / Np);
    auto patch = [&](float ox, bool isOut) {
        float px0 = ox + (colW - Np * cell) * 0.5f, py0 = row2 + 2 * u;
        for (int j = 0; j < Np; ++j)
            for (int i = 0; i < Np; ++i) {
                int x = focus.x - half + i, y = focus.y - half + j;
                bool outside = x < 0 || y < 0 || x >= imgW || y >= imgH;
                int cxp = std::clamp(x, 0, imgW - 1), cyp = std::clamp(y, 0, imgH - 1);
                bool shown = !isOut || (cyp * imgW + cxp) < reveal;
                const imgops::Image& img = isOut ? output : input;
                size_t k = img.index(cxp, cyp);
                glm::vec4 col = gray ? grayCol(img.px[k]) : glm::vec4(img.px[k] / 255.0f, img.px[k + 1] / 255.0f, img.px[k + 2] / 255.0f, 1.0f);
                if (!shown) col = glm::vec4(0.07f, 0.08f, 0.11f, 1.0f);
                if (outside) col *= glm::vec4(0.45f, 0.45f, 0.45f, 1.0f);
                float cx = px0 + i * cell, cy = py0 + j * cell;
                ui.rect(cx + 1, cy + 1, cell - 2, cell - 2, col);
                std::string label = shown ? std::to_string(img.at(cxp, cyp, ch)) : "?";
                fitted(ui, cx + cell * 0.5f, cy + cell * 0.5f, cell, label, ts * 1.1f, shown ? textOn(col) : DIM);
            }
        float kx = px0 + (half - r) * cell, ky = py0 + (half - r) * cell;
        if (!isOut) ui.outline(kx, ky, K * cell, K * cell, 2.5f, YELLOW);
        ui.outline(px0 + half * cell, py0 + half * cell, cell, cell, 2.0f, isOut ? CYAN : RED);
        return glm::vec4(kx, ky, K * cell, K * cell);
    };
    ui.text(xL + 4 * u, row2 - 13 * u, "INPUT around (" + std::to_string(focus.x) + ", " + std::to_string(focus.y) + ")" +
            (gray ? "" : "   values: " + std::string(CH_NAMES[ch])), ts, YELLOW);
    ui.text(xR + 4 * u, row2 - 13 * u, "OUTPUT around (" + std::to_string(focus.x) + ", " + std::to_string(focus.y) + ")", ts, CYAN);
    glm::vec4 zoomIn = patch(xL, false);
    patch(xR, true);
    ui.end();

    // magnifier lines: image footprint -> zoomed window
    ui.begin(W, H);
    glm::vec4 lc(1.0f, 0.85f, 0.3f, 0.25f);
    ui.line({ rectA.x, rectA.y + rectA.w }, { zoomIn.x, zoomIn.y }, 1.5f, lc);
    ui.line({ rectA.x + rectA.z, rectA.y + rectA.w }, { zoomIn.x + zoomIn.z, zoomIn.y }, 1.5f, lc);
    float pxo = xR + (colW - Np * cell) * 0.5f + half * cell, pyo = row2 + 2 * u + half * cell;
    glm::vec4 lc2(0.3f, 0.85f, 1.0f, 0.25f);
    ui.line({ rectB.x, rectB.y + rectB.w }, { pxo, pyo }, 1.5f, lc2);
    ui.line({ rectB.x + rectB.z, rectB.y + rectB.w }, { pxo + cell, pyo }, 1.5f, lc2);
    ui.end(true);

    // ================= row 2 middle: the arithmetic =================
    ui.begin(W, H);
    float mx = xM + 8 * u, my = row2 - 13 * u;
    std::string title = "COMPUTATION for this pixel" + std::string(gray ? "" : "  (channel " + std::string(CH_NAMES[ch]) + ")");
    fitted(ui, xM + midW * 0.5f, my + 3.5f * ts, midW - 8 * u, title, ts, WHITE);
    int result = outAt(focus.x, focus.y, ch);
    if (op == OP_CONVOLVE) {
        float pc = std::min((midW - 16 * u) / K, (row2H * 0.55f) / K);
        float gx = xM + (midW - K * pc) * 0.5f, gy = row2 + 4 * u;
        for (int j = 0; j < K; ++j)
            for (int i = 0; i < K; ++i) {
                float w = kernel.w[j * K + i];
                int v = inAt(focus.x + i - r, focus.y + j - r, ch);
                float prod = w * v;
                ui.rect(gx + i * pc + 1, gy + j * pc + 1, pc - 2, pc - 2,
                        prod > 0 ? glm::vec4(0.12f, 0.2f, 0.35f, 1.0f) : (prod < 0 ? glm::vec4(0.35f, 0.12f, 0.12f, 1.0f) : glm::vec4(0.13f, 0.14f, 0.18f, 1.0f)));
                fitted(ui, gx + i * pc + pc * 0.5f, gy + j * pc + pc * 0.32f, pc, num(w) + "x" + std::to_string(v), ts * 0.75f, DIM);
                fitted(ui, gx + i * pc + pc * 0.5f, gy + j * pc + pc * 0.68f, pc, num(prod), ts * 1.0f, WHITE);
            }
        float sum = 0.0f;
        convolveAt(focus.x, focus.y, ch, &sum);
        float v = sum / kernel.divisor;
        float ty = gy + K * pc + 8 * u;
        ui.text(mx, ty, "sum of products  = " + num(sum), ts, WHITE);
        ui.text(mx, ty + 13 * u, "/ " + num(kernel.divisor) + "  = " + fmt("%.2f", v), ts, WHITE);
        float yy = ty + 26 * u;
        if (kernel.absolute) { v = std::fabs(v); ui.text(mx, yy, "absolute value  = " + fmt("%.2f", v), ts, WHITE); yy += 13 * u; }
        if (kernel.offset128) { v += 128.0f; ui.text(mx, yy, "+ 128  = " + fmt("%.2f", v), ts, WHITE); yy += 13 * u; }
        ui.text(mx, yy, "round, clamp 0..255  ->  " + std::to_string(result), ts * 1.15f, CYAN);
    } else if (rank) {
        std::vector<int> vals;
        windowValues(focus.x, focus.y, ch, vals);
        int pick = op == OP_MEDIAN ? static_cast<int>(vals.size()) / 2 : (op == OP_MIN ? 0 : static_cast<int>(vals.size()) - 1);
        int perRow = std::min(9, static_cast<int>(vals.size()));
        float pc = std::min((midW - 16 * u) / perRow, 30.0f * u);
        float gx = xM + (midW - perRow * pc) * 0.5f, gy = row2 + 18 * u;
        ui.text(mx, row2 + 2 * u, "window values sorted (smallest -> largest):", ts * 0.9f, DIM);
        for (size_t i = 0; i < vals.size(); ++i) {
            float cx = gx + (i % perRow) * pc, cy = gy + (i / perRow) * pc;
            glm::vec4 col = grayCol(vals[i]);
            ui.rect(cx + 1, cy + 1, pc - 2, pc - 2, col);
            fitted(ui, cx + pc * 0.5f, cy + pc * 0.5f, pc, std::to_string(vals[i]), ts, textOn(col));
            if (static_cast<int>(i) == pick) ui.outline(cx + 1, cy + 1, pc - 2, pc - 2, 2.5f, CYAN);
        }
        float ty = gy + ((vals.size() + perRow - 1) / perRow) * pc + 10 * u;
        const char* what = op == OP_MEDIAN ? "median = middle value (#" : (op == OP_MIN ? "minimum = first value (#" : "maximum = last value (#");
        ui.text(mx, ty, what + std::to_string(pick + 1) + " of " + std::to_string(vals.size()) + ")", ts, WHITE);
        ui.text(mx, ty + 14 * u, "output  ->  " + std::to_string(result), ts * 1.15f, CYAN);
        if (op == OP_MEDIAN) ui.text(mx, ty + 30 * u, "isolated 0 / 255 noise ends up at the ends of the list", ts * 0.8f, DIM);
    } else {
        // histogram (bars) + cumulative distribution (yellow line); the current pixel's mapping
        const int Ntot = imgW * imgH;
        float cx0 = xM + 10 * u, cy0 = row2 + 4 * u, cw = midW - 20 * u, chh = row2H * 0.62f;
        ui.rect(cx0, cy0, cw, chh, glm::vec4(0.06f, 0.065f, 0.09f, 1.0f));
        int hmax = 1;
        for (int v = 0; v < 256; ++v) hmax = std::max(hmax, hist.count[v]);
        for (int v = 0; v < 256; ++v) {
            float bh = chh * hist.count[v] / static_cast<float>(hmax);
            ui.rect(cx0 + cw * v / 256.0f, cy0 + chh - bh, std::max(1.0f, cw / 256.0f), bh, glm::vec4(0.45f, 0.55f, 0.75f, 1.0f));
        }
        for (int v = 1; v < 256; ++v)
            ui.line({ cx0 + cw * (v - 1) / 256.0f, cy0 + chh - chh * hist.cdf[v - 1] / static_cast<float>(Ntot) },
                    { cx0 + cw * v / 256.0f, cy0 + chh - chh * hist.cdf[v] / static_cast<float>(Ntot) }, 2.0f, YELLOW);
        int vin = inAt(focus.x, focus.y, ch);
        float vx = cx0 + cw * (vin + 0.5f) / 256.0f, vy = cy0 + chh - chh * hist.cdf[vin] / static_cast<float>(Ntot);
        ui.line({ vx, cy0 + chh }, { vx, vy }, 1.5f, RED);
        ui.line({ vx, vy }, { cx0 + cw, vy }, 1.5f, CYAN);
        ui.text(cx0 + 4 * u, cy0 + 4 * u, "histogram h(v)", ts * 0.8f, glm::vec4(0.55f, 0.65f, 0.9f, 1.0f));
        ui.text(cx0 + 4 * u, cy0 + 16 * u, "cdf(v)", ts * 0.8f, YELLOW);
        int cdfMin = hist.cdfMin;
        float ty = cy0 + chh + 8 * u;
        ui.text(mx, ty, "v = " + std::to_string(vin) + "   cdf(v) = " + std::to_string(hist.cdf[vin]) + "   cdf_min = " + std::to_string(cdfMin) +
                "   N = " + std::to_string(Ntot), ts * 0.9f, WHITE);
        ui.text(mx, ty + 14 * u, "out = 255 x (" + std::to_string(hist.cdf[vin]) + " - " + std::to_string(cdfMin) + ") / (" +
                std::to_string(Ntot) + " - " + std::to_string(cdfMin) + ")  ->  " + std::to_string(result), ts * 0.9f, CYAN);
    }

    // ================= bottom bar: toggles + help =================
    float by = H - 62 * u, bh = 18 * u, bx = m;
    auto tog = [&](const std::string& label, bool on, float w) { bool hit = button(ui, bx, by, w * u, bh, label, on, ts); bx += w * u + 6 * u; return hit; };
    if (tog(kernel.size == 3 ? "size 3x3" : "size 5x5", false, 74)) onKey(GLFW_KEY_Z);
    if (tog("abs( )", kernel.absolute, 50)) { kernel.absolute = !kernel.absolute; dirty = true; }
    if (tog("+128", kernel.offset128, 46)) { kernel.offset128 = !kernel.offset128; dirty = true; }
    if (tog(gray ? "grey" : "RGB", gray, 46)) onKey(GLFW_KEY_G);
    if (!gray && tog(std::string("show ") + CH_NAMES[channel], false, 64)) onKey(GLFW_KEY_C);
    if (tog(std::string("noise: ") + NOISE_NAMES[noise], noise != NOISE_NONE, 132)) onKey(GLFW_KEY_I);
    if (tog(std::to_string(imgW) + "x" + std::to_string(imgH), false, 70)) { resIndex = (resIndex + 1) % RES_COUNT; inputDirty = true; restartScan(); }
    if (tog("new snapshot", false, 100)) captureRequested = true;
    if (tog(playing ? "pause" : (reveal >= N ? "replay" : "play"), playing, 60)) onKey(GLFW_KEY_SPACE);
    if (tog("slower", false, 56)) onKey(GLFW_KEY_DOWN);
    if (tog("faster", false, 56)) onKey(GLFW_KEY_UP);
    if (tog("finish", false, 56)) onKey(GLFW_KEY_ENTER);
    if (op == OP_CONVOLVE && tog("use in live CCTV (U)", sentFlash > 0.0f, 150)) onKey(GLFW_KEY_U);
    if (sentFlash > 0.0f) {
        std::string msg = "Kernel sent to the live CCTV pipeline: press ESC, then 4 or 7";
        float mw = ui.textWidth(msg, ts * 1.2f);
        ui.rect(W * 0.5f - mw * 0.5f - 10 * u, 4 * u, mw + 20 * u, 22 * u, glm::vec4(0.05f, 0.3f, 0.12f, 0.95f));
        ui.text(W * 0.5f - mw * 0.5f, 10 * u, msg, ts * 1.2f, glm::vec4(0.5f, 1.0f, 0.6f, 1.0f));
    }
    ui.text(m, H - 37 * u, "MOUSE: click a kernel cell and type a number (Enter = set, Tab = next cell)   wheel on a cell = +/-1   "
            "click any pixel of either image to inspect it", ts * 0.85f, DIM);
    ui.text(m, H - 22 * u, "KEYS: SPACE play  LEFT/RIGHT step  UP/DOWN speed  WASD move pixel  O operation  P preset  "
            "Z 3x3/5x5  G grey  C channel  I noise  -/= res  N snapshot  U use live  ESC back", ts * 0.85f, DIM);
    ui.end();
    clickPending = false;
}
