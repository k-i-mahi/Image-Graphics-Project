#pragma once

#include <glad/glad.h>
#include <string>
#include "Overlay2D.h"

// Per-stage GPU timings (GL_TIME_ELAPSED queries, read one frame late so the
// CPU never waits for the GPU), CPU simulation time and draw calls (Tab key).
enum PerfStage { PERF_SHADOW = 0, PERF_SCENE, PERF_BLOOM, PERF_DIP, PERF_UI, PERF_STAGES };

extern int g_drawCalls;   // incremented by Mesh::draw

class PerfStats {
public:
    bool visible = false;

    void init();
    void beginFrame();
    void begin(PerfStage s);
    void end(PerfStage s);
    void setCpuTimes(double frameMs, double simMs) { cpuFrame = smooth(cpuFrame, frameMs); cpuSim = smooth(cpuSim, simMs); }
    void render(Overlay2D& ui, int W, int H, const std::string& extra);

private:
    GLuint queries[2][PERF_STAGES] = {};
    bool issued[2][PERF_STAGES] = {};
    int frame = 0;
    double gpuMs[PERF_STAGES] = {};
    double cpuFrame = 0.0, cpuSim = 0.0;
    int drawCalls = 0;
    static double smooth(double old, double now) { return old == 0.0 ? now : old * 0.92 + now * 0.08; }
};
