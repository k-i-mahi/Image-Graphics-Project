# NightWatch — Changelog

Tracks what has been built, fixed and added. Newest entries at the top.

---

## 2026-10-05 (part 4) — Version 5: build fixes, bigger town

- [x] **Fixed: `build.bat` "not recognized"** when typed in PowerShell: PowerShell does not run scripts from the
  current folder by name, so the docs now say `.\build.bat`. `build.bat` itself now changes to its own folder (works
  when started from anywhere or double-clicked), checks for g++ / cmake / ninja with a clear message, fetches the
  GLFW submodule if missing, retries a stale CMake cache with `--fresh`, and pauses on failure. `run.bat` uses full paths.
- [x] **Fixed:** the exe had to be started from `build-mingw\`; it now finds `shaders\` next to itself.
- [x] **Town 2.25× larger:** 7 × 7 roads, 36 blocks. New districts: two bazaars, cricket ground with floodlights,
  hospital with helipad, two factories. 4 new vehicle routes (69 vehicles), about 160 people.
- [x] Ground plane extended so the land reaches the horizon fog; far plane 700 m.
- [x] Shadow pass skips people beyond 60 m: draw calls 6,407 → 3,159, still 60 FPS.

## 2026-10-05 (part 3) — Version 4: visuals and content

- [x] **UI**: TrueType fonts (Segoe UI, Consolas) via `stb_truetype` font atlas with mipmaps; UTF-8 text (Σ × ÷ → σ ∞);
  rounded panels, soft shadows, gradients — still one draw call per batch. Lab, HUD and performance panel all benefit.
- [x] **Lab redesign**: themed cards, pill chips, PSNR scorecard, scan progress bar, monospaced numbers, maths symbols.
- [x] **Lab content**: Otsu thresholding (σB² curve, t*) and frequency-domain filtering (separable 2D DFT, ideal/Gaussian
  low/high-pass, spectrum with pass band and cut-off ring, H(D) plot). 11 new unit-test checks (50 total).
- [x] **Rendering**: Fresnel sky reflections; static batching per material (draw calls 6,693 → ~4,400 with more content).
- [x] **Content**: coconut palms, electricity poles with sagging cables and transformers, rooftop billboards, AC units,
  solar panels, tea stall, rice paddies, village pond with boats.

## 2026-10-05 (part 2) — Version 3: restoration, analytics, measurement, engineering

- [x] **ImageOps library** (`src/ImageOps.cpp`): convolution, rank filters, histogram equalization, noise, PSNR,
  morphology, 8-connected component labelling — no OpenGL. The lab now calls it (identical results: PSNR 16.1 → 22.9 dB).
- [x] **Unit tests** (`tests/test_imageops.cpp`, `ctest`): 13 groups, 39 checks, all passing.
- [x] **CI**: GitHub Actions builds with MinGW-w64 GCC, runs the tests, uploads `NightWatch-windows-x64`.
- [x] **Live histogram equalization**: stage texture → GPU downscale → histogram → LUT texture (replaces the contrast curve).
- [x] **Denoising**: 5×5 bilateral (default), 3×3 median sorting network, or kernel (`J`); **temporal NR** (`U`).
  Default sensor noise lowered to 0.10 so the night scene is recoverable.
- [x] **Motion detection** (`B`) with CCTV HUD (camera id, clock, REC) and pipeline thumbnails.
- [x] **Bloom** from an emissive glow mask written to the alpha channel (`Z`).
- [x] **Performance panel** (`Tab`): GPU ms per stage via timer queries, CPU simulation time, draw calls.
- [x] Lab → live: `U` sends the designed kernel to views 4 and 7.
- [x] Fixed: LUT texture left bound on unit 0 tinted view 7 red; mipmap read-back of the FBO returned zeros after frame 1.
- [x] Removed the obsolete side-by-side shader mode.

## 2026-10-05 — Town world + Image Operation Lab (master plan parts A and B)

See [`ROADMAP.md`](ROADMAP.md) for the critique that motivated this pass.

### Town (replaces the single walled compound)
- [x] 5 × 5 street grid, 40 m blocks, two-lane roads with **left-hand traffic**; highways leave town on the main roads.
- [x] Road paint from world position in `scene.frag`: dashed centre line, edge lines, zebra crossings, stop lines on the approach lane, tyre tracks, black/white kerbs, footpath tiles.
- [x] 16 themed blocks: glass towers, mosque, park (fountain, Shaheed Minar memorial), 2 markets, apartments, houses, school, NightWatch HQ (the old compound, now one block), bus terminal, fuel station; outskirts with trees, village houses, hills.
- [x] New meshes: gable-roof prism, cone. New patterns: pavement, grass, plaza, water, roof tiles, glass curtain wall.
- [x] **Traffic simulation** (`Traffic.cpp`): 45 vehicles of 8 types (car, taxi, police, bus, truck, CNG, cycle rickshaw, patrol truck) on 11 routes; straight lanes + **cubic Bézier turns** and U-turns; signals at all 25 junctions; braking for red/yellow, car-following gap, stopping for pedestrians; brake lights.
- [x] **≈ 90 pedestrians** with a hierarchical skeleton and walk cycle; they wait at kerbs and cross on zebras during the parallel green; people standing in the park, at shops and the bus terminal.
- [x] Lighting scales: nearest 16 lamps + 12 spot lights (CCTV IR + headlights) per frame; 64 street lamps in total.
- [x] Shadow map follows the camera, texel-snapped.
- [x] CCTV moved to an 11 m mast at the central junction. Cameras: free → CCTV → **chase cam** (`C`).
- [x] `G` shows the patrol route and every turn's Bézier control polygon.
- [x] Shader uniform locations cached; repeated material uniforms skipped. 60 FPS (V-sync) at 1280×720.

### Image Operation Lab (view `0` / `V`, replaces side-by-side view and the 48×27 visualizer)
- [x] Original | operation | processed layout; a light source shines beams on the kernel window (input) and the output pixel.
- [x] Output revealed pixel by pixel; play/pause/step/speed; click any pixel to inspect it.
- [x] Editable kernel (click + type, wheel ±1, Tab), 3×3 / 5×5, auto/manual divisor, `abs()`, `+128`, 12 presets.
- [x] Median / min / max with the sorted window shown; **real histogram equalization** with histogram + CDF and the per-pixel mapping.
- [x] Gaussian and salt-and-pepper noise; **PSNR** of input and output vs. the clean image.

### Tooling
- [x] Command-line screenshot mode (`--shot`, `--sim`, `--sun`, `--camera`, `--view`, `--lab`, `--labkeys`) used to verify every view.
- [x] Removed superseded code: `CompoundScene`, `ConvolutionVisualizer`, `BezierPath` (Bézier maths kept in `BezierSegment`).

### Verified
- [x] Clean build, no warnings. Screenshots checked: aerial day + night, street level day + night, CCTV, chase cam, live views 4/6/8, lab with convolution, Sobel X, median on salt & pepper, histogram equalization.
- [ ] Not verifiable by screenshot: mouse clicking/typing in the lab — please try it interactively.

---

## 2026-09-23 (part 3) — Rendering overhaul ("top project" pass)

### Critique of the old 3D view (what was wrong)
1. No shadows → objects looked like they were floating.
2. Flat single-colour materials, no surface detail.
3. Sky was a single clear colour — no gradient, sun, clouds or stars.
4. No fog / aerial perspective → hard edge where the world ended.
5. No anti-aliasing → jagged edges everywhere.
6. Lighting done in display space, no tone mapping or gamma → flat, washed-out look.
7. Only 1 of the 4 lamps actually emitted light; truck headlights were fake.
8. Tiny empty world (walled box on a 100 m plane).
9. **Bug:** CCTV camera tilted *up* at the sky and its eye sat inside its own lens model → CCTV view was blank.
10. Code: `main.cpp` did everything; lighting code duplicated in two shaders.

### What was built
- [x] **Shadow mapping** — 2048² depth map from the sun/moon, 5×5 PCF soft edges, slope + normal-offset bias (`ShadowMap`, `shadow.vert/.frag`). `X` toggles.
- [x] **Procedural sky** — gradient, sun disk + glow, drifting fbm clouds, twinkling stars, moon (`sky.vert/.frag`).
- [x] **Fog** that fades exactly into the sky horizon colour.
- [x] **4× MSAA** — scene renders into a multisampled FBO, resolved into the DIP textures.
- [x] **HDR lighting pipeline** — linear-space lighting, ACES filmic tone mapping, gamma 2.2, exposure adapts day/night.
- [x] **Hemisphere ambient** (sky colour from above, ground bounce from below) + Blinn-Phong specular.
- [x] **Real night lighting** — 4 sodium lamps (point lights with finite range), CCTV IR spotlight, 2 truck headlight spotlights.
- [x] **Procedural materials** (no texture files): asphalt, grass with dirt patches, concrete with panel seams, brick with mortar relief, corrugated rusty container steel, building facades with windows that light up at night, foliage, bark, road markings.
- [x] **Bigger world** — access road, animated boom barrier, 3 shipping containers, crates & barrels, 70 trees (round + conifer), 8 city buildings, distant hills, 600 m ground.
- [x] **CCTV fixes** — lens now tilts down (+28°), mast moved to the corner and raised so the hut roof no longer blocks the view, eye placed in front of the lens.
- [x] **Enhanced DIP view** — contrast reconstruction now inverts the sensor gain with a soft highlight shoulder (was clipping to white in daylight).
- [x] **Code structure** — `Environment` module owns sun/sky/lights; shaders share `common.glsl` / `lighting.glsl` via a new `#include` in the shader loader.

### Verified
- [x] Clean build (no warnings). Screenshots checked: day overview, sunset, night, CCTV day/night, degraded, enhanced, split screen, side-by-side, Phong/Gouraud/Flat. Runs at 60 FPS (V-sync).

---

## 2026-09-23 (part 2) — Sun, side-by-side filters, step-by-step convolution

### New features
- [x] **Sun with time of day** — `[` / `]` move the sun, `T` automatic cycle. Sun colour (orange at the horizon,
  white at noon), sky colour, directional light and street-lamp brightness follow it; moon takes over at night.
  Sun and moon drawn as glowing spheres. Current time shown in the window title.
- [x] **View `0`: side-by-side filter compare** — original camera view on the left, filtered on the right,
  with labels and the kernel matrix printed under the filtered image.
- [x] **`V`: step-by-step convolution visualizer** — camera snapshot turned into a 48×27 grayscale grid; the kernel
  slides one cell at a time; input window + output cell highlighted; bottom panel shows
  patch × kernel = products → sum → ÷ divisor → clamp → value. Play/pause, single step, speed, finish, restart,
  change filter, new snapshot.
- [x] **Shared kernel table** (`include/Kernels.h`) — GPU filters and the visualizer use exactly the same kernels.
  Added 3 new filters: **Laplacian edge, Sobel X, Emboss** (7 total, `K` cycles).
- [x] **2D overlay renderer** (`Overlay2D`) for on-screen text and boxes (font: `stb_easy_font.h`, public domain).

### Verified
- [x] Builds with no errors/warnings; checked day scene, night scene, view 0 (Laplacian), visualizer with
  Laplacian, Sobel X (finished) and Gaussian 5×5 (numbers fit their boxes) via screenshots.

---

## 2026-09-23 — Build fix + new features

### Build / run fixes
- [x] **Fixed broken build.** `build.bat` looked for Visual Studio BuildTools at
  `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\...`, which does not exist on this PC.
- [x] Switched to the installed **MinGW g++ 13.2 + CMake + Ninja** (`C:\cpsetup-main\bin`).
  Build output now goes to `build-mingw\` (clean build ≈ 1 min, incremental ≈ seconds).
- [x] `CMakeLists.txt`: MinGW links the C/C++ runtime statically → `NightWatch.exe` runs without extra DLLs.
- [x] `run.bat` / `run.ps1` build first, then launch from `build-mingw\` so `shaders/` are found.
- [x] `build.ps1` / `run.ps1` call `build.bat` by full path (it failed with "not recognized" otherwise).
- [x] Original MSVC script kept as `build_msvc.bat`.

### Bug fixes
- [x] **Key conflict:** `S` moved the camera backward *and* cycled shading at the same time.
  Shading toggle moved to **`F`**.
- [x] **V-sync enabled** (`glfwSwapInterval(1)`): FPS capped to monitor refresh (was ~1400 FPS,
  wasting GPU and tearing).

### New features
- [x] **`F11` Fullscreen toggle** — switches to the monitor's native resolution and back to the previous window size/position.
- [x] **`8` Sobel Edge Detection view** — gradient magnitude `G = sqrt(Gx² + Gy²)` on luminance;
  outlines drawn in yellow over a darkened scene.
- [x] **`9` Night Vision view** — luminance amplified (`pow(4L, 0.7)`), mapped to green phosphor,
  Gaussian grain (Box-Muller), circular goggle mask.
- [x] **`F12` Screenshot** — saves the displayed frame as `screenshot_<date>_<time>.bmp` in `build-mingw\`
  (plain 24-bit BMP writer, no extra library).
- [x] Startup help text and `README.md` controls table updated for `F`, `8`, `9`, `F11`, `F12`.

### Verified
- [x] Builds with no errors/warnings.
- [x] App launches and responds; view modes 8 and 9 switch correctly; F12 screenshots saved and checked visually.

---

## Already done before 2026-09-23 (original project)

- [x] 3D procedural compound: walls, gate, watchtower, guard hut, lampposts, patrol truck
- [x] 4-DOF hierarchical CCTV camera (base → yaw → pitch → lens) with auto-pan sweep
- [x] Cubic Bézier patrol path with tangent-based heading and wheel rotation
- [x] Tri-source lighting: moonlight (directional), sodium lamp (point), IR spotlight (spot)
- [x] Flat / Gouraud / Phong shading
- [x] FBO capturing color + depth textures
- [x] DIP views 1–7: pristine, CCTV feed, degraded (low light + Gaussian noise), enhanced
  (convolution + contrast stretch), depth map, depth-of-field, split-screen compare
- [x] Filters: 3×3 Gaussian, 5×5 Gaussian, 3×3 Box, 3×3 Laplacian sharpen

---

## Ideas / next steps (not done yet)

- [ ] Bloom around bright lamps / windows
- [ ] Screen-space ambient occlusion (SSAO) for contact shadows
- [ ] Frame-difference **motion detection** (moving truck highlighted in red)
- [ ] Visualizer on colour (R, G, B channels separately) instead of grayscale
- [ ] Let the user type a custom kernel
- [ ] On-screen **luminance histogram** overlay
- [ ] Fog / rain in the 3D scene
- [ ] Timestamp + camera ID text on CCTV view
- [ ] Median filter (true salt-and-pepper denoising) as an extra `K` filter option
