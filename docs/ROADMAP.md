# NightWatch — Design Review & Roadmap

Goal: every Computer Graphics and every
Image Processing concept is **implemented from first principles, visible on screen, and explainable
with a formula**.

---

## 1. Design review of version 1

### 3D world
| # | Problem | Impact |
|---|---|---|
| 1 | The world is one walled box in an empty field. Buildings are 8 random boxes, one road strip. | Looks like a toy, not a "scene". Nothing for the camera to watch. |
| 2 | Only **one** moving object (the truck), on one loop. No people at all. | A *surveillance* project with nobody to survey. |
| 3 | Roads have no lanes, junctions, crossings, signals. | No structure — the scene does not "make sense". |
| 4 | Light system has a hard limit of 4 lamps + 3 spots. | Night scene cannot scale beyond the compound. |
| 5 | Shadow map is a fixed 90 m box at the origin. | Any larger world loses its shadows. |
| 6 | Every uniform is looked up by name on every draw (`glGetUniformLocation`). | Slow once the scene grows. |

### Image processing
| # | Problem | Impact |
|---|---|---|
| 7 | Filters are only shown as *finished* full-screen results. | The examiner sees **that** it changed, not **how** a pixel is computed. |
| 8 | The step-by-step view (`V`) works on a 48×27 grey grid on a separate screen. | Disconnected from the real image; feels like a different program. |
| 9 | Kernels are hard-coded. You cannot type your own. | "What happens if I change this weight?" — no answer. |
| 10 | Proposal promises **histogram equalization**; code does a sigmoid curve "mimicking" it. | A teacher who reads the proposal will catch this. |
| 11 | No non-linear filters (median, min/max), no salt-and-pepper noise. | Missing the most classic DIP demo (median vs. salt & pepper). |
| 12 | No quantitative evaluation (PSNR / MSE) although proposal §6 promises it. | "Quantitative analysis of noise reduction" is unmet. |

---

## 2. Target design

### Part A — Town world (CG)  (done)
* **Street grid**: 5 × 5 roads, 40 m blocks, two lanes, **left-hand traffic** (Bangladesh),
  dashed centre line, edge lines, zebra crossings, stop lines — all drawn procedurally in the
  fragment shader from world position (no textures).
* **16 city blocks**, each with a purpose: downtown towers, mosque (dome + minarets), park with
  fountain, market/shops with awnings and neon signs, apartments with balconies and roof water
  tanks, houses with pitched roofs, school with playground, police/NightWatch HQ, bus/parking lot,
  fuel station.
* **Traffic simulation**: cars, taxis, police car (flashing light bar), buses, trucks, CNG
  auto-rickshaws, cycle rickshaws (with pedalling rider).
  * Routes = straight lanes + **cubic Bézier corner turns** (proposal §3.2 — tangent `P'(t)` gives heading).
  * **Traffic lights** at every junction (green → yellow → all-red cycle).
  * Vehicles **brake for red lights, for the vehicle ahead, and for pedestrians**; brake lights glow.
* **Pedestrians**: hierarchical skeleton (pelvis → torso → head, shoulder → arm, hip → thigh → knee → shin),
  walk cycle from `sin(phase)`, wait at kerbs for the signal, cross on zebra crossings.
* **Lighting at scale**: up to 16 nearest street lamps (point) + 12 spot lights (CCTV IR + nearest headlights).
* **Camera-following shadow map** with texel snapping (no shimmering).
* **CCTV** on a 11 m mast at the central junction, 4-DOF chain unchanged (proposal §3.1).
* Camera modes: Free fly → CCTV → Chase-cam on the patrol car.

### Part B — Image Operation Lab (DIP)  (done)
One screen, three columns:

```
 ┌──────────── ORIGINAL ───────────┐   ┌─ OPERATION ─┐   ┌──────────── PROCESSED ──────────┐
 │                                 │   │  ( light )  │   │                                 │
 │          █ ← pixel  ◄═══ beam ══╪═══╪══►  ●  ◄════╪═══╪══ beam ═══► █ ← output pixel   │
 │                                 │   │ ┌─┬─┬─┐     │   │   (revealed as the scan moves)  │
 └─────────────────────────────────┘   │ │1│2│1│ ←editable kernel                          │
 ┌ zoomed input patch with values ┐    │ ├─┼─┼─┤     │   ┌ zoomed output patch + values  ┐
 │  52  60  71                    │    │ │2│4│2│ ÷16 │   │                                │
 │  58 [64] 80                    │    │ └─┴─┴─┘     │   │                                │
 └────────────────────────────────┘    └─────────────┘   └────────────────────────────────┘
   Σ products = 1·52 + 2·60 + … = 1046   ÷16 = 65.4  → clamp → 65
```

* A **light source** above the operation panel shines one beam onto the kernel footprint in the
  original image and one onto the output pixel in the processed image; the beams follow the
  pixel as it moves.
* Pixel chosen by **mouse** (hover/click) or by **auto-scan** (raster order, adjustable speed); the
  processed image is **revealed** as the scan passes, so you watch the output being built.
* **Editable kernel** (3×3 / 5×5): click a cell, type a number; divisor auto or manual;
  presets: identity, box, Gaussian, sharpen, Laplacian, Sobel X/Y, Prewitt, emboss, motion blur.
* Operations: convolution, **median / min / max** (shows the sorted neighbourhood),
  point ops (negative, threshold, gamma), **true histogram equalization** (histogram + CDF drawn).
* Input: live snapshot of the town (CCTV) with optional **Gaussian** or **salt-and-pepper** noise.
* **PSNR / MSE** vs. the clean image shown live → quantitative noise-reduction analysis (proposal §6).

### Part C — Polish & report
* Real histogram equalization also in the live CCTV pipeline (view 4).
* Motion detection by frame differencing (moving cars/people boxed in red) — ties CG and DIP together.
* Timestamp / camera ID burned into the CCTV feed.
* Performance panel (ms per stage) for the "computational overhead" outcome in proposal §6.
* Update README, controls, screenshots for the report.

---

## 2b. Design review of version 2 (after the town and the lab)

| # | Problem | Fix (version 3) |
|---|---|---|
| 1 | Live "enhanced" view was a fixed contrast curve, not histogram equalization; 3×3 blur barely denoised | Two-pass pipeline: stage texture → GPU-downscaled read-back → histogram → LUT. Bilateral / median / kernel denoise, motion-adaptive temporal NR |
| 2 | The CCTV only *showed* the town — no image understanding | Motion detection: background subtraction, opening, connected components, live insets |
| 3 | Proposal outcome "evaluate the computational overhead" unmet | GPU timer queries per stage + draw-call counter (`Tab`) |
| 4 | Night looked flat — emitters did not glow | Glow mask in alpha + separable-Gaussian bloom |
| 5 | Lab kernels and live kernels were separate worlds | `U` in the lab sends the kernel to the live views |
| 6 | Algorithms lived inside UI code; no tests, no CI | `ImageOps` library, 39 unit-test checks, GitHub Actions build + test + artifact |
| 7 | Mipmap read-back of an FBO attachment returned zeros after the first frame on this driver | Replaced by a blit-downscale + `glReadPixels` (`Downsampler`) |

## 2c. Design review of version 3 (visuals and content)

| # | Problem | Fix (version 4) |
|---|---|---|
| 1 | Pixel-font UI made the lab look like a debug tool | TrueType atlas (stb_truetype), rounded cards, shadows, colour-coded sections, PSNR scorecard |
| 2 | Lab stopped at spatial filters | Otsu segmentation and frequency-domain filtering (2D DFT, ideal/Gaussian LP/HP) with spectrum view |
| 3 | Glass, water and paint looked matte | Fresnel sky reflections |
| 4 | Trees were green balls; streets empty | Coconut palms, power poles with sagging cables, billboards, AC units, solar panels, tea stall |
| 5 | Outskirts were a flat green plane | Rice paddies with dikes, village pond with boats |
| 6 | 6,700 draw calls left no room for detail | Static batching per material |

## 3. Order of work

1. [x] **Town world + traffic + people** (Part A) — done 2026-10-05
2. [x] **Image Operation Lab** (Part B) — done 2026-10-05
3. [x] Real histogram equalization + PSNR + median/salt-and-pepper (in the lab) — done 2026-10-05
4. [x] Real histogram equalization in the live CCTV pipeline (view 4) — done (v3)
5. [x] Motion detection, timestamp/camera-ID HUD, per-stage timing panel — done (v3)
6. [x] Bloom, bilateral + temporal denoising, unit tests, CI — done (v3)
7. [x] Frequency-domain filtering + Otsu in the lab, TrueType UI, reflections, street detail — done (v4)
8. [ ] Ideas: CLAHE, detection accuracy vs. simulator ground truth, screen-space ambient occlusion, rain
