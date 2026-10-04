#include "PerfStats.h"
#include <algorithm>
#include <cstdio>

int g_drawCalls = 0;

static const char* STAGE_NAMES[PERF_STAGES] = { "shadow map pass", "3D scene (MSAA)", "bloom", "image processing", "2D overlay / lab" };

void PerfStats::init() {
    glGenQueries(PERF_STAGES, queries[0]);
    glGenQueries(PERF_STAGES, queries[1]);
}

void PerfStats::beginFrame() {
    // Collect the results of the queries issued two frames ago (same buffer), then reuse them
    frame ^= 1;
    for (int s = 0; s < PERF_STAGES; ++s) {
        if (!issued[frame][s]) continue;
        GLuint64 ns = 0;
        glGetQueryObjectui64v(queries[frame][s], GL_QUERY_RESULT, &ns);
        gpuMs[s] = smooth(gpuMs[s], ns / 1.0e6);
        issued[frame][s] = false;
    }
    drawCalls = g_drawCalls;
    g_drawCalls = 0;
}

void PerfStats::begin(PerfStage s) { glBeginQuery(GL_TIME_ELAPSED, queries[frame][s]); }

void PerfStats::end(PerfStage s) {
    glEndQuery(GL_TIME_ELAPSED);
    issued[frame][s] = true;
}

void PerfStats::render(Overlay2D& ui, int W, int H, const std::string& extra) {
    if (!visible) return;
    const float u = H / 720.0f, ts = 1.45f * u, lh = 17.0f * u;
    const float x = 14 * u, y = 62 * u, w = 300 * u;
    double gpuTotal = 0.0;
    for (double v : gpuMs) gpuTotal += v;
    char line[128];

    ui.begin(W, H);
    ui.rect(x, y, w, lh * (PERF_STAGES + 7) + 12 * u, glm::vec4(0.02f, 0.03f, 0.05f, 0.78f));
    float ty = y + 8 * u;
    ui.text(x + 8 * u, ty, "PERFORMANCE  (Tab)", ts * 1.1f, glm::vec4(1.0f, 0.82f, 0.22f, 1.0f));
    ty += lh * 1.3f;
    std::snprintf(line, sizeof(line), "frame %.2f ms  (%.0f fps)", cpuFrame, cpuFrame > 0 ? 1000.0 / cpuFrame : 0.0);
    ui.text(x + 8 * u, ty, line, ts, glm::vec4(1.0f));
    ty += lh;
    std::snprintf(line, sizeof(line), "CPU simulation  %.2f ms", cpuSim);
    ui.text(x + 8 * u, ty, line, ts, glm::vec4(0.75f, 0.8f, 0.9f, 1.0f));
    ty += lh * 1.2f;
    for (int s = 0; s < PERF_STAGES; ++s) {
        std::snprintf(line, sizeof(line), "GPU %-17s %5.2f ms", STAGE_NAMES[s], gpuMs[s]);
        ui.text(x + 8 * u, ty, line, ts, glm::vec4(0.75f, 0.8f, 0.9f, 1.0f));
        float bar = static_cast<float>(std::min(1.0, gpuMs[s] / 8.0)) * (w - 16 * u);
        ui.rect(x + 8 * u, ty + 12 * u, bar, 2.0f * u, glm::vec4(0.3f, 0.75f, 1.0f, 0.9f));
        ty += lh;
    }
    std::snprintf(line, sizeof(line), "GPU total %.2f ms   draw calls %d", gpuTotal, drawCalls);
    ty += 2 * u;
    ui.text(x + 8 * u, ty, line, ts, glm::vec4(1.0f));
    ty += lh;
    ui.text(x + 8 * u, ty, extra, ts, glm::vec4(0.75f, 0.8f, 0.9f, 1.0f));
    ui.end();
}
