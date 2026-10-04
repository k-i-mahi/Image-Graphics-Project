#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <vector>
#include <ctime>
#include <cmath>
#include <algorithm>

#include "Shader.h"
#include "Camera.h"
#include "Kinematics.h"
#include "Geometry.h"
#include "Lighting.h"
#include "FBO.h"
#include "DIPProcessor.h"
#include "Town.h"
#include "Traffic.h"
#include "Overlay2D.h"
#include "FilterLab.h"
#include "Bloom.h"
#include "CctvAnalytics.h"
#include "PerfStats.h"
#include "Environment.h"
#include "ShadowMap.h"

float simWarmup = 0.0f;   // seconds of traffic simulated before the first frame (--sim)
bool labAtStart = false;  // --lab
bool motionAtStart = false, perfAtStart = false;   // --motion, --perf
std::string labKeys;      // --labkeys
bool labWasActive = false;

// Window dimensions
int screenWidth = 1280;
int screenHeight = 720;

// Cameras: free fly, the CCTV lens, or a chase camera behind the patrol car
enum CameraMode { CAM_FREE = 0, CAM_CCTV = 1, CAM_CHASE = 2 };
const glm::vec3 START_POS(58.0f, 34.0f, 92.0f);
const float START_YAW = -122.0f, START_PITCH = -20.0f;
Camera freeCamera(START_POS, glm::vec3(0.0f, 1.0f, 0.0f), START_YAW, START_PITCH);
CameraMode cameraMode = CAM_FREE;
glm::vec3 chaseEye(0.0f, 10.0f, 30.0f), chaseTarget(0.0f);
float lastX = screenWidth / 2.0f;
float lastY = screenHeight / 2.0f;
bool firstMouse = true;

// Timing
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Pipeline Modules
GeometryManager geoManager;
CCTVKinematicChain cctvChain(glm::vec3(7.6f, 11.0f, 7.6f));   // mast on the corner of the central junction
TownScene town;
TrafficSystem traffic;
FramebufferObject fboBridge;
DIPProcessor dipProcessor;

// Sun, sky and all lights + sun shadow map
Environment env;
ShadowMap shadowMap;

// Input State Tracking
bool keys[1024];
bool keysProcessed[1024];

// Function Prototypes
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow* window);
void printHelpGuide();
void toggleFullscreen(GLFWwindow* window);
void handleDisplayKeys(GLFWwindow* window);
bool keyPressedOnce(GLFWwindow* window, int key);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void char_callback(GLFWwindow* window, unsigned int codepoint);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);

// 2D overlay + step-by-step convolution demo
Overlay2D overlay;
FilterLab lab;   // view 0 / V: the Image Operation Lab
Bloom bloom;                 // glow of lamps, windows, headlights (Z)
CctvAnalytics analytics;     // motion detection + CCTV HUD (B)
PerfStats perf;              // per-stage GPU timings (Tab)
glm::mat4 lastViewMatrix(0.0f);
bool sweepBeforeMotion = true;
void saveScreenshot(int width, int height);
bool screenshotRequested = false;
std::string screenshotName;

// Command-line automation (renders, saves one screenshot, exits):
//   --shot out.bmp --frames 60 --sun 55 --view 1 --camera free|cctv|chase --sim 20
//   --cam x y z yaw pitch --size 1280x720 --guide
struct AutoShot { bool on = false; std::string file; int frames = 60; } autoShot;
void parseArgs(int argc, char** argv);
const char* cameraName();

// Windowed-mode geometry, restored when leaving fullscreen
int windowedX = 100, windowedY = 100, windowedW = 1280, windowedH = 720;

int main(int argc, char** argv) {
    parseArgs(argc, argv);
    std::cout << "======================================================================\n";
    std::cout << " NightWatch: Closed-Loop OpenGL Rendering & Spatial DIP Pipeline\n";
    std::cout << " Department of Computer Science and Engineering, KUET\n";
    std::cout << " Course: CSE 4102 | Author: Khadimul Islam Mahi (Roll: 2107076)\n";
    std::cout << "======================================================================\n" << std::flush;

    // 1. Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "[FATAL] Failed to initialize GLFW." << std::endl;
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // 2. Create Window
    GLFWwindow* window = glfwCreateWindow(screenWidth, screenHeight,
        "NightWatch | Closed-Loop CG & DIP Pipeline [KUET CSE 4102]", NULL, NULL);
    if (!window) {
        std::cerr << "[FATAL] Failed to create GLFW window." << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // V-sync: cap to monitor refresh rate
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCharCallback(window, char_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);

    // Capture mouse for smooth 3D free inspection
    if (!autoShot.on) glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // 3. Initialize GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "[FATAL] Failed to initialize GLAD." << std::endl;
        return -1;
    }

    // 4. Configure OpenGL State
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // 5. Initialize Procedural Geometries, the town and its traffic
    geoManager.init();
    town.init(geoManager);
    traffic.init();
    cctvChain.yawAngle = 225.0f;     // looks diagonally across the junction (-x, -z)
    cctvChain.minYaw = 165.0f;
    cctvChain.maxYaw = 285.0f;
    cctvChain.pitchAngle = 20.0f;
    cctvChain.sweepSpeed = 10.0f;
    for (float t = 0.0f; t < simWarmup; t += 0.05f) { traffic.update(0.05f); cctvChain.update(0.05f); }

    // 6. Load Shaders
    Shader sceneShader("shaders/scene.vert", "shaders/scene.frag");
    Shader dipShader("shaders/quad.vert", "shaders/dip.frag");
    Shader skyShader("shaders/sky.vert", "shaders/sky.frag");
    Shader shadowShader("shaders/shadow.vert", "shaders/shadow.frag");

    // 7. Initialize Framebuffer Object Bridge
    if (!fboBridge.init(screenWidth, screenHeight)) {
        std::cerr << "[FATAL] Failed to initialize FBO Bridge." << std::endl;
        return -1;
    }

    // 8. Sun shadow map (2048 x 2048 depth texture)
    if (!shadowMap.init(2048)) {
        std::cerr << "[FATAL] Failed to initialize shadow map." << std::endl;
        return -1;
    }

    if (!overlay.init()) {
        std::cerr << "[FATAL] Failed to initialize 2D overlay." << std::endl;
        return -1;
    }
    if (!lab.init()) {
        std::cerr << "[FATAL] Failed to initialize the Image Operation Lab." << std::endl;
        return -1;
    }
    if (labAtStart) lab.open();
    if (!bloom.init(screenWidth, screenHeight) || !analytics.init() || !dipProcessor.initGL(screenWidth, screenHeight)) {
        std::cerr << "[FATAL] Failed to initialize post-processing." << std::endl;
        return -1;
    }
    perf.init();
    perf.visible = perfAtStart;
    if (motionAtStart) { analytics.motionEnabled = true; cctvChain.autoSweep = false; }
    printHelpGuide();

    float fpsTimer = 0.0f;
    int frameCount = 0;
    int autoFrame = 0;

    // 9. Main Render Loop
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // FPS calculation & Title Update
        fpsTimer += deltaTime;
        frameCount++;
        if (fpsTimer >= 0.5f) {
            float fps = static_cast<float>(frameCount) / fpsTimer;
            std::stringstream title;
            title << "NightWatch | " << dipProcessor.getViewModeName()
                  << " | Cam: " << cameraName()
                  << " | Shading: " << (town.currentShadingModel == SHADING_PHONG ? "Phong" :
                                       (town.currentShadingModel == SHADING_GOURAUD ? "Gouraud" : "Flat"))
                  << " | Time " << env.clockString()
                  << " | Filter: " << dipProcessor.getFilterName()
                  << " | FPS: " << std::fixed << std::setprecision(1) << fps;
            glfwSetWindowTitle(window, title.str().c_str());
            frameCount = 0;
            fpsTimer = 0.0f;
        }

        // Process User Inputs
        processInput(window);
        perf.beginFrame();

        // Update Dynamic Simulations
        double simStart = glfwGetTime();
        float simDt = autoShot.on ? 1.0f / 60.0f : deltaTime;   // screenshots: fixed step
        cctvChain.update(simDt);
        traffic.update(simDt);
        env.update(simDt);                                      // sun, sky, fog, exposure
        town.lampGlow = env.lampGlow();

        // Determine Active View & Projection Matrices
        glm::mat4 viewMatrix;
        glm::vec3 activeViewPos, focus;
        if (cameraMode == CAM_CCTV) {
            viewMatrix = cctvChain.getCCTVViewMatrix();
            activeViewPos = cctvChain.getLensWorldPosition();
            focus = activeViewPos + cctvChain.getLensForwardDirection() * 35.0f;
        } else if (cameraMode == CAM_CHASE && !traffic.vehicles.empty()) {
            const Vehicle& pv = traffic.vehicles[traffic.patrolVehicle];
            // eye sits on the route 14 m behind the car, so it stays above the road at corners
            glm::vec3 wantEye = traffic.routes[pv.route].sample(pv.s - 14.0f) + glm::vec3(0.0f, 6.5f, 0.0f);
            glm::vec3 wantTarget = pv.pos + pv.fwd * 6.0f + glm::vec3(0.0f, 1.5f, 0.0f);
            float k = 1.0f - std::exp(-4.0f * simDt);           // smooth exponential follow
            chaseEye = glm::mix(chaseEye, wantEye, k);
            chaseTarget = glm::mix(chaseTarget, wantTarget, k);
            viewMatrix = glm::lookAt(chaseEye, chaseTarget, glm::vec3(0.0f, 1.0f, 0.0f));
            activeViewPos = chaseEye;
            focus = pv.pos;
        } else {
            viewMatrix = freeCamera.GetViewMatrix();
            activeViewPos = freeCamera.Position;
            focus = freeCamera.Position + freeCamera.Front * 45.0f;
        }
        focus.y = 0.0f;

        // Local lights near the viewer: street lamps, CCTV IR, vehicle headlights
        std::vector<SpotSource> spots;
        float night = 1.0f - env.dayFactor;
        spots.push_back({ cctvChain.getLensWorldPosition(), cctvChain.getLensForwardDirection(),
                          glm::vec3(0.7f, 0.8f, 1.0f) * 3.0f * (1.0f - 0.7f * env.dayFactor),
                          glm::vec2(std::cos(glm::radians(18.0f)), std::cos(glm::radians(26.0f))) });
        if (night > 0.01f) {
            for (const Vehicle& v : traffic.vehicles) {
                if (v.type == VT_RICKSHAW) continue;
                spots.push_back({ v.pos + v.fwd * (v.length * 0.5f + 0.2f) + glm::vec3(0.0f, 0.8f, 0.0f),
                                  glm::normalize(v.fwd + glm::vec3(0.0f, -0.12f, 0.0f)),
                                  glm::vec3(1.0f, 0.93f, 0.8f) * 5.0f * night,
                                  glm::vec2(std::cos(glm::radians(20.0f)), std::cos(glm::radians(38.0f))) });
            }
        }
        env.setLocalLights(town.lampPositions(), spots, activeViewPos);
        perf.setCpuTimes(deltaTime * 1000.0, (glfwGetTime() - simStart) * 1000.0);
        env.setShadowCenter(focus);
        town.cullCenter = activeViewPos;
        float aspect = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);
        glm::mat4 projMatrix = glm::perspective(glm::radians(45.0f), aspect, dipProcessor.nearPlane, dipProcessor.farPlane);

        // The lab shows a frozen snapshot: the 3D scene is only drawn when it needs a new one
        bool render3D = !lab.active || lab.captureRequested;
        if (render3D) {
            // ====================================================================
            // STAGE 0: SHADOW MAP - scene depth as seen from the sun / moon
            // ====================================================================
            if (env.shadowsEnabled) {
                perf.begin(PERF_SHADOW);
                shadowMap.begin();
                shadowShader.use();
                shadowShader.setMat4("uLightSpace", env.lightSpaceMatrix());
                town.render(shadowShader, geoManager, cctvChain, traffic, currentFrame, true);
                shadowMap.end();
                perf.end(PERF_SHADOW);
            }

            // ====================================================================
            // STAGE 1: 3D GRAPHICS SYNTHESIS -> RENDER TO FBO BRIDGE (4x MSAA)
            // ====================================================================
            perf.begin(PERF_SCENE);
            fboBridge.bind();
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Sky: full-screen gradient, sun, clouds, stars (depth stays at the far plane)
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            skyShader.use();
            env.applyToSkyShader(skyShader, viewMatrix, projMatrix, currentFrame);
            geoManager.drawQuad();
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);

            sceneShader.use();
            sceneShader.setMat4("uView", viewMatrix);
            sceneShader.setMat4("uProjection", projMatrix);
            sceneShader.setVec3("uViewPos", activeViewPos);
            sceneShader.setFloat("uTime", currentFrame);
            env.applyToSceneShader(sceneShader);
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, shadowMap.depthTexture);
            sceneShader.setInt("uShadowMap", 5);
            glActiveTexture(GL_TEXTURE0);

            // Draw the town
            town.render(sceneShader, geoManager, cctvChain, traffic, currentFrame, false);

            fboBridge.resolve();
            fboBridge.unbind();
            perf.end(PERF_SCENE);
        }

        // Bloom from the glow mask (alpha) of the camera image
        GLuint bloomTex = 0;
        if (render3D && bloom.enabled && !lab.active) {
            perf.begin(PERF_BLOOM);
            bloomTex = bloom.run(geoManager, fboBridge.getColorTexture());
            perf.end(PERF_BLOOM);
        }

        // Motion detection on the camera frame; the background restarts whenever the camera moves
        if (!lab.active && analytics.motionEnabled) {
            bool moved = glm::length(glm::vec4(viewMatrix[3] - lastViewMatrix[3])) > 1e-3f ||
                         glm::length(glm::vec3(viewMatrix[0] - lastViewMatrix[0])) > 1e-4f ||
                         glm::length(glm::vec3(viewMatrix[2] - lastViewMatrix[2])) > 1e-4f;
            analytics.update(fboBridge.fboID, fboBridge.width, fboBridge.height, simDt, moved);
        }
        lastViewMatrix = viewMatrix;

        if (lab.active && lab.captureRequested) {
            lab.captureRequested = false;
            lab.capture(fboBridge.getColorTexture(), fboBridge.width, fboBridge.height);
            // --labkeys: scripted key presses for screenshots ('>' right, '<' left, '^' up, '_' down, '!' enter)
            for (char c : labKeys) {
                int key = c == '>' ? GLFW_KEY_RIGHT : c == '<' ? GLFW_KEY_LEFT : c == '^' ? GLFW_KEY_UP :
                          c == '_' ? GLFW_KEY_DOWN : c == '!' ? GLFW_KEY_ENTER : static_cast<int>(c);
                lab.update(0.0f);
                lab.onKey(key);
            }
            labKeys.clear();
        }

        // ====================================================================
        // STAGE 2: SPATIAL IMAGE PROCESSING (DIP) -> RENDER TO SCREEN QUAD
        // ====================================================================
        glViewport(0, 0, screenWidth, screenHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        perf.begin(PERF_DIP);
        if (lab.active) {
            // Image Operation Lab replaces the DIP output
            lab.update(autoShot.on ? 1.0f / 60.0f : deltaTime);
            lab.render(overlay, geoManager, screenWidth, screenHeight, currentFrame);
            if (lab.sendToLive) {   // U in the lab: the designed kernel becomes the live CCTV filter
                lab.sendToLive = false;
                const LabKernel& k = lab.currentKernel();
                std::fill(dipProcessor.customKernel, dipProcessor.customKernel + 25, 0.0f);
                for (int i = 0; i < k.size * k.size; ++i) dipProcessor.customKernel[i] = k.w[i] / k.divisor;
                dipProcessor.customSize = k.size;
                dipProcessor.customAbs = k.absolute;
                dipProcessor.customName = "Lab: " + lab.kernelName();
                dipProcessor.useCustomKernel = true;
                dipProcessor.denoise = DENOISE_KERNEL;
                std::cout << "[LAB] Kernel sent to the live CCTV pipeline (views 4 and 7)" << std::endl;
            }
        } else {
            dipProcessor.renderPostProcess(dipShader, geoManager,
                                          fboBridge.getColorTexture(),
                                          fboBridge.getDepthTexture(),
                                          bloomTex, bloom.strength,
                                          screenWidth, screenHeight,
                                          currentFrame);
        }
        perf.end(PERF_DIP);

        // HUD, motion boxes and the performance panel
        perf.begin(PERF_UI);
        if (!lab.active) {
            int vm = dipProcessor.currentViewMode;
            bool hud = cameraMode == CAM_CCTV && vm != VIEW_PRISTINE && vm != VIEW_DEPTH_MAP && vm != VIEW_DOF;
            analytics.render(overlay, geoManager, screenWidth, screenHeight, env.clockString(), hud, currentFrame);
        }
        std::string extra = std::to_string(traffic.vehicles.size()) + " vehicles  " + std::to_string(traffic.pedestrians.size()) +
                            " people  " + std::to_string(town.lampPositions().size()) + " lamps";
        perf.render(overlay, screenWidth, screenHeight, extra);
        perf.end(PERF_UI);

        // Mouse cursor: free in the lab, captured for the fly camera
        if (lab.active != labWasActive && !autoShot.on) {
            glfwSetInputMode(window, GLFW_CURSOR, lab.active ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
            firstMouse = true;
            labWasActive = lab.active;
        }

        if (autoShot.on && ++autoFrame == autoShot.frames) {
            screenshotName = autoShot.file;
            screenshotRequested = true;
        }
        if (screenshotRequested) {
            screenshotRequested = false;
            saveScreenshot(screenWidth, screenHeight);
            if (autoShot.on) glfwSetWindowShouldClose(window, true);
        }

        // Swap Buffers and Poll Events
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup Resources
    fboBridge.cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

void processInput(GLFWwindow* window) {
    handleDisplayKeys(window);

    // The lab receives its keys through key_callback / char_callback
    if (lab.active) return;

    if (keyPressedOnce(window, GLFW_KEY_ESCAPE))
        glfwSetWindowShouldClose(window, true);

    // Image Operation Lab: 0 or V (works on a snapshot of the current camera view)
    if (keyPressedOnce(window, GLFW_KEY_0) || keyPressedOnce(window, GLFW_KEY_V)) {
        lab.open();
        std::cout << "[LAB] Image Operation Lab opened" << std::endl;
        return;
    }

    // Sun position: [ / ]  (hold), T = automatic day/night cycle
    if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS)
        env.sunAngle = std::fmod(env.sunAngle - 40.0f * deltaTime + 360.0f, 360.0f);
    if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS)
        env.sunAngle = std::fmod(env.sunAngle + 40.0f * deltaTime, 360.0f);
    if (keyPressedOnce(window, GLFW_KEY_T)) {
        env.autoCycle = !env.autoCycle;
        std::cout << "[SUN] Automatic day/night cycle: " << (env.autoCycle ? "ON" : "OFF") << std::endl;
    }
    if (keyPressedOnce(window, GLFW_KEY_X)) {
        env.shadowsEnabled = !env.shadowsEnabled;
        std::cout << "[SHADOWS] " << (env.shadowsEnabled ? "ON" : "OFF") << std::endl;
    }

    // Free camera navigation (WASD + Q/E)
    if (cameraMode == CAM_FREE) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            freeCamera.ProcessKeyboard(FORWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            freeCamera.ProcessKeyboard(BACKWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            freeCamera.ProcessKeyboard(LEFT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            freeCamera.ProcessKeyboard(RIGHT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
            freeCamera.ProcessKeyboard(UP, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            freeCamera.ProcessKeyboard(DOWN, deltaTime);
    }

    // 1-7: View Mode Selection
    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) dipProcessor.currentViewMode = VIEW_PRISTINE;
    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) {
        dipProcessor.currentViewMode = VIEW_CCTV_RAW;
        cameraMode = CAM_CCTV;
    }
    if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS) dipProcessor.currentViewMode = VIEW_DEGRADED;
    if (glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS) dipProcessor.currentViewMode = VIEW_ENHANCED;
    if (glfwGetKey(window, GLFW_KEY_5) == GLFW_PRESS) dipProcessor.currentViewMode = VIEW_DEPTH_MAP;
    if (glfwGetKey(window, GLFW_KEY_6) == GLFW_PRESS) dipProcessor.currentViewMode = VIEW_DOF;
    if (glfwGetKey(window, GLFW_KEY_7) == GLFW_PRESS) dipProcessor.currentViewMode = VIEW_SPLIT_SCREEN;
    if (glfwGetKey(window, GLFW_KEY_8) == GLFW_PRESS) dipProcessor.currentViewMode = VIEW_EDGES;
    if (glfwGetKey(window, GLFW_KEY_9) == GLFW_PRESS) dipProcessor.currentViewMode = VIEW_NIGHT_VISION;

    // Cycle Camera: C  (free -> CCTV -> chase the patrol car)
    if (keyPressedOnce(window, GLFW_KEY_C)) {
        cameraMode = static_cast<CameraMode>((cameraMode + 1) % 3);
        if (cameraMode == CAM_CHASE && !traffic.vehicles.empty()) {
            const Vehicle& pv = traffic.vehicles[traffic.patrolVehicle];
            chaseEye = traffic.routes[pv.route].sample(pv.s - 14.0f) + glm::vec3(0.0f, 6.5f, 0.0f);
            chaseTarget = pv.pos;
        }
        std::cout << "[CAMERA] Switched to: " << cameraName() << std::endl;
    }

    // Toggle Shading Model: F (Phong -> Gouraud -> Flat)
    if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS && !keysProcessed[GLFW_KEY_F]) {
        keysProcessed[GLFW_KEY_F] = true;
        int next = (static_cast<int>(town.currentShadingModel) + 1) % 3;
        town.currentShadingModel = static_cast<ShadingModel>(next);
        const char* sNames[] = { "Phong Shading", "Gouraud Shading", "Flat Shading" };
        std::cout << "[SHADING] Active Shading Model: " << sNames[next] << std::endl;
    }
    if (glfwGetKey(window, GLFW_KEY_F) == GLFW_RELEASE) keysProcessed[GLFW_KEY_F] = false;

    // Toggle Convolution Filter: K
    if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS && !keysProcessed[GLFW_KEY_K]) {
        keysProcessed[GLFW_KEY_K] = true;
        if (dipProcessor.useCustomKernel || dipProcessor.denoise != DENOISE_KERNEL) dipProcessor.useCustomKernel = false;
        else dipProcessor.filterIndex = (dipProcessor.filterIndex + 1) % CONV_KERNEL_COUNT;
        dipProcessor.denoise = DENOISE_KERNEL;
        std::cout << "[DIP FILTER] Active Filter: " << dipProcessor.getFilterName() << std::endl;
    }
    if (glfwGetKey(window, GLFW_KEY_K) == GLFW_RELEASE) keysProcessed[GLFW_KEY_K] = false;

    // Denoise method for views 4 / 7: J cycles bilateral -> median -> convolution kernel
    if (keyPressedOnce(window, GLFW_KEY_J)) {
        dipProcessor.denoise = dipProcessor.denoise == DENOISE_BILATERAL ? DENOISE_MEDIAN
                             : (dipProcessor.denoise == DENOISE_MEDIAN ? DENOISE_KERNEL : DENOISE_BILATERAL);
        std::cout << "[DIP] Denoise: " << dipProcessor.getFilterName() << std::endl;
    }
    // Motion detection: B (the CCTV stops panning so the background can be learned)
    if (keyPressedOnce(window, GLFW_KEY_B)) {
        analytics.motionEnabled = !analytics.motionEnabled;
        analytics.reset();
        if (analytics.motionEnabled) { sweepBeforeMotion = cctvChain.autoSweep; cctvChain.autoSweep = false; }
        else cctvChain.autoSweep = sweepBeforeMotion;
        std::cout << "[CCTV] Motion detection " << (analytics.motionEnabled ? "ON" : "OFF") << std::endl;
    }
    if (keyPressedOnce(window, GLFW_KEY_Z)) {
        bloom.enabled = !bloom.enabled;
        std::cout << "[POST] Bloom " << (bloom.enabled ? "ON" : "OFF") << std::endl;
    }
    if (keyPressedOnce(window, GLFW_KEY_TAB)) perf.visible = !perf.visible;
    if (keyPressedOnce(window, GLFW_KEY_U)) {
        dipProcessor.temporalNR = !dipProcessor.temporalNR;
        std::cout << "[DIP] Temporal noise reduction " << (dipProcessor.temporalNR ? "ON" : "OFF") << std::endl;
    }

    // Cycle light groups: L
    if (keyPressedOnce(window, GLFW_KEY_L)) {
        env.cycleLights();
        std::cout << "[LIGHTING] " << env.lightStateName() << std::endl;
    }

    // Toggle Pause Bézier Patrol: SPACE
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && !keysProcessed[GLFW_KEY_SPACE]) {
        keysProcessed[GLFW_KEY_SPACE] = true;
        traffic.paused = !traffic.paused;
        std::cout << "[TRAFFIC] Vehicles, people and signals " << (traffic.paused ? "PAUSED" : "RESUMED") << std::endl;
    }
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_RELEASE) keysProcessed[GLFW_KEY_SPACE] = false;

    // Toggle Histogram Stretch / Equalization: H
    if (glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS && !keysProcessed[GLFW_KEY_H]) {
        keysProcessed[GLFW_KEY_H] = true;
        dipProcessor.enableHistogramStretch = !dipProcessor.enableHistogramStretch;
        std::cout << "[DIP] Histogram equalization (live): "
                  << (dipProcessor.enableHistogramStretch ? "ON" : "OFF") << std::endl;
    }
    if (glfwGetKey(window, GLFW_KEY_H) == GLFW_RELEASE) keysProcessed[GLFW_KEY_H] = false;

    // Toggle CCTV Auto Sweep: P
    if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS && !keysProcessed[GLFW_KEY_P]) {
        keysProcessed[GLFW_KEY_P] = true;
        cctvChain.autoSweep = !cctvChain.autoSweep;
        std::cout << "[CCTV] Kinematic Auto-Pan Sweep: " << (cctvChain.autoSweep ? "ON" : "OFF") << std::endl;
    }
    if (glfwGetKey(window, GLFW_KEY_P) == GLFW_RELEASE) keysProcessed[GLFW_KEY_P] = false;

    // Toggle Bézier Route Guide markers: G
    if (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS && !keysProcessed[GLFW_KEY_G]) {
        keysProcessed[GLFW_KEY_G] = true;
        town.drawBezierGuide = !town.drawBezierGuide;
        std::cout << "[SCENE] Patrol route + Bezier control points: "
                  << (town.drawBezierGuide ? "VISIBLE" : "HIDDEN") << std::endl;
    }
    if (glfwGetKey(window, GLFW_KEY_G) == GLFW_RELEASE) keysProcessed[GLFW_KEY_G] = false;

    // Adjust DoF Focal Distance Zfocus: UP / DOWN
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
        dipProcessor.dofFocalDepth += 12.0f * deltaTime;
        if (dipProcessor.dofFocalDepth > 70.0f) dipProcessor.dofFocalDepth = 70.0f;
    }
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
        dipProcessor.dofFocalDepth -= 12.0f * deltaTime;
        if (dipProcessor.dofFocalDepth < 2.0f) dipProcessor.dofFocalDepth = 2.0f;
    }

    // Adjust Split Screen Divider Position: LEFT / RIGHT
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        dipProcessor.splitPosition -= 0.4f * deltaTime;
        if (dipProcessor.splitPosition < 0.05f) dipProcessor.splitPosition = 0.05f;
    }
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        dipProcessor.splitPosition += 0.4f * deltaTime;
        if (dipProcessor.splitPosition > 0.95f) dipProcessor.splitPosition = 0.95f;
    }

    // Adjust Sensor Noise Level: N / M
    if (glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS) {
        dipProcessor.noiseIntensity += 0.15f * deltaTime;
        if (dipProcessor.noiseIntensity > 0.8f) dipProcessor.noiseIntensity = 0.8f;
    }
    if (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS) {
        dipProcessor.noiseIntensity -= 0.15f * deltaTime;
        if (dipProcessor.noiseIntensity < 0.0f) dipProcessor.noiseIntensity = 0.0f;
    }

    // Reset Camera Position: R
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS && !keysProcessed[GLFW_KEY_R]) {
        keysProcessed[GLFW_KEY_R] = true;
        freeCamera = Camera(START_POS, glm::vec3(0.0f, 1.0f, 0.0f), START_YAW, START_PITCH);
        std::cout << "[CAMERA] Reset to initial perspective." << std::endl;
    }
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_RELEASE) keysProcessed[GLFW_KEY_R] = false;
}

// Writes the current back buffer to a 24-bit BMP file (no external libraries)
void saveScreenshot(int width, int height) {
    int rowSize = (width * 3 + 3) & ~3; // BMP rows are 4-byte aligned
    std::vector<unsigned char> pixels(rowSize * height);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_BGR, GL_UNSIGNED_BYTE, pixels.data()); // bottom-up, as BMP expects

    char stamp[64];
    std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "screenshot_%Y%m%d_%H%M%S.bmp", std::localtime(&now));
    std::string name = screenshotName.empty() ? std::string(stamp) : screenshotName;
    screenshotName.clear();

    unsigned int dataSize = rowSize * height;
    unsigned char header[54] = { 'B', 'M' };
    auto put32 = [&](int off, unsigned int v) { for (int i = 0; i < 4; ++i) header[off + i] = (v >> (8 * i)) & 0xFF; };
    put32(2, 54 + dataSize); // file size
    put32(10, 54);           // pixel data offset
    put32(14, 40);           // info header size
    put32(18, width);
    put32(22, height);
    header[26] = 1;          // planes
    header[28] = 24;         // bits per pixel
    put32(34, dataSize);

    std::ofstream out(name, std::ios::binary);
    out.write(reinterpret_cast<char*>(header), 54);
    out.write(reinterpret_cast<char*>(pixels.data()), dataSize);
    std::cout << "[SCREENSHOT] Saved " << name << std::endl;
}

// Edge-triggered key press (true once per physical press)
bool keyPressedOnce(GLFWwindow* window, int key) {
    if (glfwGetKey(window, key) == GLFW_PRESS) {
        if (!keysProcessed[key]) { keysProcessed[key] = true; return true; }
    } else {
        keysProcessed[key] = false;
    }
    return false;
}

void handleDisplayKeys(GLFWwindow* window) {
    // Toggle Fullscreen: F11
    if (glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS && !keysProcessed[GLFW_KEY_F11]) {
        keysProcessed[GLFW_KEY_F11] = true;
        toggleFullscreen(window);
    }
    if (glfwGetKey(window, GLFW_KEY_F11) == GLFW_RELEASE) keysProcessed[GLFW_KEY_F11] = false;

    // Screenshot: F12 (captured after the DIP pass this frame)
    if (glfwGetKey(window, GLFW_KEY_F12) == GLFW_PRESS && !keysProcessed[GLFW_KEY_F12]) {
        keysProcessed[GLFW_KEY_F12] = true;
        screenshotRequested = true;
    }
    if (glfwGetKey(window, GLFW_KEY_F12) == GLFW_RELEASE) keysProcessed[GLFW_KEY_F12] = false;
}

// Keys go to the lab while it is open. Marking them as processed stops the
// polling code from seeing the same press again (e.g. Esc closing the lab
// must not also quit the program).
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (!lab.active || action == GLFW_RELEASE || key < 0 || key >= 1024) return;
    if (key == GLFW_KEY_F11 || key == GLFW_KEY_F12) return;
    lab.onKey(key);
    keysProcessed[key] = true;
}

void char_callback(GLFWwindow* window, unsigned int codepoint) {
    if (lab.active) lab.onChar(codepoint);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (lab.active && button == GLFW_MOUSE_BUTTON_LEFT) lab.onMouseButton(action == GLFW_PRESS);
}

void toggleFullscreen(GLFWwindow* window) {
    if (glfwGetWindowMonitor(window)) {
        glfwSetWindowMonitor(window, NULL, windowedX, windowedY, windowedW, windowedH, 0);
        std::cout << "[DISPLAY] Windowed mode" << std::endl;
    } else {
        glfwGetWindowPos(window, &windowedX, &windowedY);
        glfwGetWindowSize(window, &windowedW, &windowedH);
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        std::cout << "[DISPLAY] Fullscreen " << mode->width << "x" << mode->height << std::endl;
    }
    firstMouse = true; // avoid a camera jump from the cursor re-centering
}

const char* cameraName() {
    switch (cameraMode) {
        case CAM_CCTV:  return "CCTV Mount";
        case CAM_CHASE: return "Chase (patrol car)";
        default:        return "Free Roam";
    }
}

void parseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* def) -> std::string { return i + 1 < argc ? argv[++i] : def; };
        if (a == "--shot") { autoShot.on = true; autoShot.file = next("shot.bmp"); }
        else if (a == "--frames") autoShot.frames = std::max(2, std::atoi(next("60").c_str()));
        else if (a == "--sun") env.sunAngle = static_cast<float>(std::atof(next("55").c_str()));
        else if (a == "--view") dipProcessor.currentViewMode = static_cast<ViewMode>(std::atoi(next("1").c_str()));
        else if (a == "--sim") simWarmup = static_cast<float>(std::atof(next("0").c_str()));
        else if (a == "--guide") town.drawBezierGuide = true;
        else if (a == "--lab") labAtStart = true;
        else if (a == "--motion") motionAtStart = true;
        else if (a == "--perf") perfAtStart = true;
        else if (a == "--nobloom") bloom.enabled = false;
        else if (a == "--kernel") dipProcessor.denoise = DENOISE_KERNEL;
        else if (a == "--labkeys") labKeys = next("");   // keys typed into the lab at start, e.g. "OOI"
        else if (a == "--camera") {
            std::string c = next("free");
            cameraMode = c == "cctv" ? CAM_CCTV : (c == "chase" ? CAM_CHASE : CAM_FREE);
        } else if (a == "--cam" && i + 5 < argc) {
            float x = std::atof(argv[i + 1]), y = std::atof(argv[i + 2]), z = std::atof(argv[i + 3]);
            float yaw = std::atof(argv[i + 4]), pitch = std::atof(argv[i + 5]);
            i += 5;
            freeCamera = Camera(glm::vec3(x, y, z), glm::vec3(0.0f, 1.0f, 0.0f), yaw, pitch);
        } else if (a == "--size") {
            std::string v = next("1280x720");
            size_t xpos = v.find('x');
            if (xpos != std::string::npos) {
                screenWidth = std::max(320, std::atoi(v.substr(0, xpos).c_str()));
                screenHeight = std::max(240, std::atoi(v.substr(xpos + 1).c_str()));
            }
        }
    }
}

void mouse_callback(GLFWwindow* window, double xposIn, double yposIn) {
    if (lab.active) { lab.onMouseMove(static_cast<float>(xposIn), static_cast<float>(yposIn)); return; }
    if (cameraMode != CAM_FREE || autoShot.on) return; // CCTV / chase orientation is driven by the simulation

    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; // reversed since y-coordinates go from bottom to top

    lastX = xpos;
    lastY = ypos;

    freeCamera.ProcessMouseMovement(xoffset, yoffset);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    if (lab.active) { lab.onScroll(static_cast<float>(yoffset)); return; }
    freeCamera.ProcessMouseScroll(static_cast<float>(yoffset));
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    if (width > 0 && height > 0) {
        screenWidth = width;
        screenHeight = height;
        fboBridge.resize(width, height);
        bloom.resize(width, height);
        dipProcessor.resize(width, height);
        glViewport(0, 0, width, height);
    }
}

void printHelpGuide() {
    std::cout << "\n======================================================================\n";
    std::cout << " KEYBOARD SHORTCUTS & DEMONSTRATION CONTROLS (Show to Teacher):\n";
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << " [1] Pristine 3D Scene    : Direct full-quality graphics render\n";
    std::cout << " [2] CCTV Live Feed       : Raw view from mounted 4-DOF camera\n";
    std::cout << " [3] Degraded Sensor Feed : Low-light sensor noise (Gaussian grain)\n";
    std::cout << " [4] Enhanced DIP Feed    : Denoised (Spatial convolution) + contrast\n";
    std::cout << " [5] Depth Buffer Map     : Linearized view-space distance visualization\n";
    std::cout << " [6] Depth-of-Field (DoF) : Equation 2: R(x,y) = alpha * |Z - Zfocus|\n";
    std::cout << " [7] Split-Screen Compare : Left=Degraded, Right=Enhanced DIP\n";
    std::cout << " [8] Sobel Edge Detection : Gradient magnitude G = sqrt(Gx^2 + Gy^2)\n";
    std::cout << " [9] Night Vision         : Green phosphor image intensifier\n";
    std::cout << " [0] or [V] IMAGE OPERATION LAB : original | editable kernel + light beams | processed\n";
    std::cout << "           convolution, median, min, max, histogram equalization; noise; PSNR\n";
    std::cout << "           click a kernel cell + type a value, click pixels, SPACE play, arrows step/speed, ESC back\n";
    std::cout << " [ [ / ] ] : Move the sun (time of day)   [T] Auto day/night cycle   [X] Shadows on/off\n";
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << " [C]     : Cycle Camera (Free fly -> CCTV mount -> Chase the patrol car)\n";
    std::cout << " [F]     : Toggle Shading Model (Phong -> Gouraud -> Flat)\n";
    std::cout << " [F11]   : Toggle Fullscreen\n";
    std::cout << " [F12]   : Save Screenshot (.bmp)\n";
    std::cout << " [L]     : Cycle Lights (All -> Sun/Moon -> Street lamps -> CCTV IR + headlights)\n";
    std::cout << " [K]     : Live filter kernel (Gaussian 3x3/5x5, Box, Sharpen, Laplacian, Sobel X, Emboss)\n";
    std::cout << " [J]     : Live denoise: 5x5 bilateral -> 3x3 median -> kernel   [U] temporal NR\n";
    std::cout << " [H]     : Live histogram equalization on/off\n";
    std::cout << " [B]     : CCTV motion detection (background subtraction, morphology, components)\n";
    std::cout << " [Z]     : Bloom on/off      [Tab] Performance panel (GPU ms per stage)\n";
    std::cout << " [P]     : Toggle CCTV Kinematic Auto-Pan Sweep\n";
    std::cout << " [G]     : Show patrol route + Bezier control points P0..P3 of every turn\n";
    std::cout << " [SPACE] : Pause / Resume traffic, pedestrians and signals\n";
    std::cout << " [UP/DN] : Adjust DoF Focal Plane Zfocus\n";
    std::cout << " [LT/RT] : Adjust Split-Screen Divider position\n";
    std::cout << " [N / M] : Increase / Decrease Sensor Noise Intensity\n";
    std::cout << " [R]     : Reset Camera Position\n";
    std::cout << " [W,A,S,D,Q,E] + Mouse : Free Roam Camera Flight Navigation\n";
    std::cout << "======================================================================\n\n" << std::flush;
}
