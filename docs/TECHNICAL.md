# NightWatch — Project Details

**Course:** CSE 4102 — Computer Graphics and Image Processing Laboratory, KUET
**Author:** Khadimul Islam Mahi (Roll: 2107076)
**Language / API:** C++17, OpenGL 3.3 Core, GLSL 330

See [`ROADMAP.md`](ROADMAP.md) for the critique of the earlier version and the remaining roadmap.

---

## 1. What the project is

NightWatch renders a living 3D **town** watched by a CCTV camera (Computer Graphics), then treats the
rendered image as a **camera sensor feed** and processes it (Digital Image Processing):
degrade it like a cheap low-light CCTV sensor, then denoise / enhance / analyse it — and, in the
**Image Operation Lab**, shows pixel by pixel *how* each operation transforms the picture.

```text
 traffic + pedestrian simulation ──► shadow pass ──► sky + town (4x MSAA, HDR, tone map, fog) ──► FBO colour + depth
                                                                                                     │
                                       live DIP shader (views 1-9) ◄────────────────────────────────┤
                                       Image Operation Lab (view 0, CPU, pixel-exact) ◄─────────────┘
```

---

## 2. Tech stack

| Part | Library / tool | Location |
|---|---|---|
| Window, input, fullscreen | GLFW (built from source) | `extern/glfw/` |
| OpenGL function loader | GLAD (OpenGL 3.3 Core) | `extern/glad/` |
| Math (vectors, matrices) | GLM (header-only) | `include/glm/` |
| Text | `stb_easy_font.h` (public domain) | `include/` |
| Build system | CMake + Ninja | `CMakeLists.txt` |
| Compiler | MinGW-w64 g++ 13.2 (`C:\cpsetup-main\bin`) | — |

No external image/model assets — every object, texture pattern and person is generated in code.

---

## 3. Source files

| File | Role |
|---|---|
| `src/main.cpp` | Window, render loop, cameras (free / CCTV / chase), input, screenshots, command-line shot mode |
| `include/Town.h`, `src/Town.cpp` | Street grid constants, `Painter` draw helper, the 16 city blocks, lamps, outskirts |
| `src/TownActors.cpp` | Animated parts: traffic signals, 8 vehicle models, hierarchical people, CCTV, fountain, Bézier guide |
| `include/Traffic.h`, `src/Traffic.cpp` | Lane routes (straights + Bézier turns), signal cycle, vehicle and pedestrian behaviour |
| `include/Bezier.h` | Cubic Bézier segment `P(t)`, `P'(t)` |
| `src/Kinematics.cpp` | 4-DOF CCTV kinematic chain (yaw/pitch, auto-sweep) |
| `src/Geometry.cpp` | Procedural cube, cylinder, sphere, cone, gable-roof prism, ground, screen quad |
| `src/Environment.cpp` | Time of day, sky, fog, exposure; picks the nearest 16 lamps + 12 spot lights; camera-following shadow box |
| `src/ShadowMap.cpp` | Depth framebuffer for sun/moon shadows |
| `src/FBO.cpp` | Offscreen framebuffer with colour + depth textures (4x MSAA resolve) |
| `src/DIPProcessor.cpp` | Uniforms for the live DIP shader (`dip.frag`), view/filter names |
| `include/Kernels.h` | Kernel table used by the live GPU filters (`K`) |
| `include/FilterLab.h`, `src/FilterLab.cpp` | **Image Operation Lab** (view `0` / `V`) |
| `src/Overlay2D.cpp` | 2D rectangles, gradient triangles, lines, text; additive "light" blending |
| `src/Shader.cpp` | Loads/compiles GLSL with `#include`, caches uniform locations |
| `shaders/scene.vert/.frag` | Flat / Gouraud / Phong, shadows, procedural materials (roads, kerbs, facades, glass, roof tiles, water …) |
| `shaders/lighting.glsl` | Hemisphere ambient, directional, 16 point, 12 spot lights, Blinn-Phong |
| `shaders/common.glsl` | hash / value noise / fbm |
| `shaders/sky.*`, `shadow.*`, `quad.vert`, `dip.frag` | Sky, shadow pass, full-screen quad, live image processing |
| `shaders/image.vert/.frag` | Pixel-exact image panels of the lab (with the "not processed yet" hatching) |
| `shaders/overlay.vert/.frag` | 2D overlay |

---

## 4. Computer Graphics features

### 4.1 The town
- **Street grid:** 5 × 5 roads, 40 m blocks, two lanes, **left-hand traffic** (Bangladesh). The two main roads
  continue out of town as highways.
- **Road markings from world position** (fragment shader, no textures): dashed centre line, edge lines,
  **zebra crossings** at every junction, **stop lines** only on the approaching lane, tyre-worn lanes,
  painted black/white kerbs, footpath tiles.
- **16 blocks with a purpose:** downtown glass towers, mosque (dome, minarets, ablution pool), park with fountain
  and a Shaheed Minar memorial, two market blocks (shop fronts, awnings, neon signs, roof water tanks),
  apartments (balconies, water tanks), houses with tiled gable roofs, school (brick, playground, national flag),
  NightWatch HQ (walled compound, watch tower, radio mast, containers), bus terminal, fuel station.
- **Outskirts:** 110 trees, tin-roof village houses, hills fading into fog.

### 4.2 Traffic and people (simulation + hierarchical modelling)
- **Vehicles:** car, taxi, police car (flashing light bar), bus, cargo truck, **CNG auto-rickshaw**,
  **cycle rickshaw** (rider pedals with the crank angle), NightWatch patrol truck — 45 in total.
- **Routes:** straight lane pieces + **cubic Bézier turns**; a quarter-circle turn uses handles of
  `0.5523 r` (standard cubic arc approximation); highway ends use a Bézier U-turn.
- **Heading from the tangent:** `θ = atan2(P'x, P'z)`. **Wheel roll:** `angle += Δs / r`.
- **Behaviour:** stop at red, stop at yellow if `d > v² / 2a`, keep a gap to the vehicle ahead
  (`v ≤ v_lead + k (gap − gap_min)`), stop for pedestrians on the road; brake lights glow when decelerating.
- **Traffic signals** at every junction: X green 11 s → yellow 2.5 s → all-red 1 s → Z green 11 s → yellow 2 s → all-red 1 s.
- **Pedestrians (≈ 90):** hierarchical skeleton pelvis → torso → head, shoulder → elbow, hip → knee;
  walk cycle `swing = 0.45 sin(φ)`, knee bend `max(0, sin(φ + 1.6))`; saree/salwar, lungi or shirt-trousers;
  they wait at the kerb and cross on the zebra during the parallel green.

### 4.3 Rendering
- **Shadow mapping:** 2048² depth map, orthographic 150 m box that **follows the camera**, centre snapped to whole
  texels (no shimmering), 5×5 PCF, slope-scaled + normal-offset bias.
- **Many lights:** each frame the nearest 16 street lamps (point, `I0 / (a + b·d + c·d²)` × range window) and
  12 spot lights (CCTV IR + nearest headlights) are sent to the shader.
- **HDR pipeline:** linear lighting → exposure → ACES filmic → gamma 2.2 → distance fog matched to the sky horizon.
- **4× MSAA**, procedural sky (sun, moon, clouds, stars), time of day (`[` / `]`, `T`).
- **Shading models:** Flat, Gouraud, Phong (`F`).
- **Cameras (`C`):** free fly → CCTV (4-DOF chain `M_lens = M_base · R_y(yaw) · T_arm · R_x(pitch) · T_lens`) → chase
  camera behind the patrol truck (eye placed on the route 14 m behind, smoothed with `1 − e^(−4Δt)`).
- **Performance:** uniform locations cached, material uniforms skipped when unchanged, distant people/vehicles culled;
  60 FPS (V-sync) at 1280×720 on this PC.

---

## 5. Image Processing features

### 5.1 Live CCTV pipeline (GPU, views 1-9)

| View | Technique |
|---|---|
| 3 Degraded | Intensity dampening `I·κ` + Gaussian noise via Box-Muller `Z = sqrt(-2 ln U1) cos(2π U2)` |
| 4 Enhanced | Spatial convolution (`K` kernel) + contrast stretch |
| 5 Depth map | Linearised depth from the depth buffer |
| 6 Depth of field | Blur radius `R(x,y) = α·|Z(x,y) − Z_focus|`, 16-tap disc kernel |
| 7 Split screen | Degraded vs enhanced, movable divider |
| 8 Sobel edges | `G = sqrt(Gx² + Gy²)` on luminance |
| 9 Night vision | Luminance gain + green phosphor + grain + goggle mask |

### 5.2 Image Operation Lab (view `0` or `V`)

```text
 ORIGINAL (input)          |   OPERATION  ( light )   |   PROCESSED (output)
 kernel window lit yellow  |   editable kernel        |   output pixel lit cyan, image revealed pixel by pixel
 zoomed input + values     |   the arithmetic         |   zoomed output + values ("?" = not computed yet)
```

- Snapshot of the current camera view, reduced to 64×36 … 320×180 so single pixels are visible.
- A **light source** above the operation panel throws one beam onto the kernel window in the original and one
  onto the output pixel in the processed image; magnifier lines connect them to the zoomed neighbourhoods.
- The processed image is **built in raster order** (1 … 20000 px/s, step one pixel with ← / →).
- **Operations:** convolution (editable 3×3 / 5×5 kernel, divisor auto or manual, `abs()`, `+128`, 12 presets),
  **median**, **min (erosion)**, **max (dilation)** — showing the sorted window — and **histogram equalization**
  (histogram + CDF drawn, mapping of the current pixel; colour images are equalized on luminance).
- **Noise:** none / Gaussian (σ = 25) / salt-and-pepper (8 %), and **PSNR = 10 log10(255² / MSE)** of input and output
  against the clean image — quantitative noise-reduction analysis.
- Border handling: clamp-to-edge (dimmed cells in the zoom show the repeated edge values).

---

## 6. Build & run

```cmd
build.bat      :: configure + compile into build-mingw\
run.bat        :: build (if needed) and launch
```

Executable: `build-mingw\NightWatch.exe` (run with `build-mingw\` as working directory so it finds `shaders\`).

Screenshot mode (used to check renders without touching the keyboard):

```cmd
NightWatch.exe --shot town.bmp --frames 60 --sim 20 --sun 55 --camera free|cctv|chase --view 1
NightWatch.exe --shot lab.bmp --camera cctv --lab --labkeys "OII^^"
```

---

## 7. Controls

| Key | Action |
|---|---|
| `1`–`9` | Live views (1 = scene, 2 = CCTV feed, 3 degraded, 4 enhanced, 5 depth, 6 DoF, 7 split, 8 Sobel, 9 night vision) |
| `0` or `V` | Image Operation Lab |
| `C` | Camera: free fly → CCTV → chase the patrol truck |
| `[` / `]`, `T` | Move the sun / automatic day-night |
| `X` | Shadows on/off |
| `F` | Shading: Phong → Gouraud → Flat |
| `L` | Lights: all → sun/moon → street lamps → spotlights (IR + headlights) |
| `K` | Live filter (views 4 / 7) |
| `H` | Contrast stretch on/off |
| `P` | CCTV auto-pan on/off |
| `G` | Patrol route + Bézier control points |
| `Space` | Pause/resume traffic, people and signals |
| `↑` / `↓` | DoF focus distance |
| `←` / `→` | Split-screen divider |
| `N` / `M` | Sensor noise up / down |
| `R` | Reset camera |
| `W A S D Q E` + mouse | Fly camera |
| `F11` / `F12` | Fullscreen / screenshot (`.bmp`) |
| `Esc` | Quit |

**Inside the lab:** click a kernel cell and type a number (`Enter` set, `Tab` next cell, mouse wheel ±1);
click any pixel of either image; `Space` play/pause, `←`/`→` one pixel, `↑`/`↓` speed, `WASD` move the inspected
pixel, `O` operation, `P` preset, `Z` 3×3/5×5, `G` grey/RGB, `C` channel, `I` noise, `-`/`=` resolution,
`N` new snapshot, `Esc` back.

---

## 8. Demo order for the teacher (suggested)

1. `1`, fly over the town (`W A S D` + mouse). `[` / `]` day → sunset → night (lamps, windows, headlights). `F` shading, `L` lights.
2. `C` to the CCTV camera (4-DOF chain), `C` again for the chase cam; `G` to show the Bézier control points of each turn.
   Point out signals, cars braking at red, pedestrians waiting and crossing.
3. `0` — the lab: let Gaussian 3×3 run slowly, pause, step with `→`, explain the numbers; click a kernel cell,
   type a new weight, watch the result change.
4. `I` (salt & pepper) → Gaussian vs `O` median: compare PSNR. Sobel X/Y, Laplacian (`abs()`), Emboss (`+128`).
5. `O` to histogram equalization: histogram, CDF and the mapping of one pixel.
6. Back to live views: `3` → `4` → `7`, then `5` depth, `6` depth of field, `8` Sobel, `9` night vision.
