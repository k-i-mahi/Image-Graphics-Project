#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D uColorTexture;
uniform sampler2D uDepthTexture;

// View Mode
// 1 = Pristine, 2 = CCTV Live, 3 = Degraded Sensor, 4 = Enhanced DIP,
// 5 = Depth Map, 6 = Depth of Field, 7 = Split Screen,
// 8 = Sobel Edge Detection, 9 = Night Vision, 10 = Side-by-Side Filter Compare
uniform int uViewMode;

// Convolution kernel (normalized weights, row-major, up to 5x5) - see Kernels.h
uniform float uKernel[25];
uniform int uKernelSize;
uniform bool uKernelAbs;

// Degradation parameters
uniform float uNoiseIntensity;
uniform float uLowLightDampening;
uniform float uTime;
uniform vec2 uScreenResolution;

// Enhancement parameters
uniform float uContrastStretch;
uniform bool uEnableEqualization;

// Depth-driven Depth of Field
uniform float uFocalDepth;
uniform float uDofAlpha;
uniform float uNearPlane;
uniform float uFarPlane;

// Split screen parameter
uniform float uSplitPosition; // 0.0 to 1.0

#define PI 3.14159265359

// Linearize normalized depth buffer value to camera view-space distance in world units
float linearizeDepth(float depth) {
    float z = depth * 2.0 - 1.0; // back to NDC [-1, 1]
    return (2.0 * uNearPlane * uFarPlane) / (uFarPlane + uNearPlane - z * (uFarPlane - uNearPlane));
}

// High-quality pseudo-random hash generator [0, 1]
float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// Box-Muller transform to generate standard normal Gaussian noise ~ N(0, 1)
float generateGaussianNoise(vec2 coord, float timeVal) {
    float u1 = max(hash21(coord + vec2(timeVal * 0.17, timeVal * 0.31)), 1e-6);
    float u2 = hash21(coord + vec2(timeVal * 0.73, timeVal * 0.59) + 43.12);
    return sqrt(-2.0 * log(u1)) * cos(2.0 * PI * u2);
}

// Degrades pristine pixel to simulate low-light security sensor noise
vec3 degradeColor(vec3 pristine, vec2 coord) {
    // 1. Pixel intensity dampening (low-light optical attenuation)
    vec3 dampened = pristine * uLowLightDampening;

    // 2. Sensor Gaussian noise superposition
    float noise = generateGaussianNoise(coord * uScreenResolution, uTime) * uNoiseIntensity;
    vec3 noisy = dampened + vec3(noise);

    return clamp(noisy, 0.0, 1.0);
}

// Spatial Convolution Filtering
// Generic k x k kernel uploaded from Kernels.h (row 0 = top row of the image window).
// When 'degrade' is true each sample first passes through the sensor degradation model.
vec3 applyKernel(vec2 uv, bool degrade) {
    vec2 texelSize = 1.0 / uScreenResolution;
    int r = uKernelSize / 2;
    vec3 result = vec3(0.0);
    for (int ky = 0; ky < uKernelSize; ++ky) {
        for (int kx = 0; kx < uKernelSize; ++kx) {
            vec2 sampleUV = uv + vec2(float(kx - r), float(r - ky)) * texelSize;
            vec3 c = texture(uColorTexture, sampleUV).rgb;
            if (degrade) c = degradeColor(c, sampleUV);
            result += c * uKernel[ky * uKernelSize + kx];
        }
    }
    if (uKernelAbs) result = abs(result);
    return clamp(result, 0.0, 1.0);
}

// Contrast Stretching and Dynamic Range Enhancement
vec3 enhanceContrast(vec3 color) {
    if (!uEnableEqualization) return color;

    // Undo the sensor's low-light gain loss, then a normalized exponential shoulder:
    // lifts dark/mid tones strongly while highlights roll off instead of clipping.
    vec3 stretched = pow(color * uContrastStretch, vec3(0.9));
    const float k = 1.2;
    return clamp((1.0 - exp(-k * stretched)) / (1.0 - exp(-k)), 0.0, 1.0);
}

// Equation 2: R(x, y) = alpha * |Z(x, y) - Zfocus|
vec3 applyDepthOfField(vec2 uv) {
    float rawDepth = texture(uDepthTexture, uv).r;
    float linearZ = linearizeDepth(rawDepth);

    // Compute blur radius
    float blurRadius = clamp(uDofAlpha * abs(linearZ - uFocalDepth), 0.0, 8.0);

    if (blurRadius < 0.2) {
        return texture(uColorTexture, uv).rgb;
    }

    vec2 texelSize = (1.0 / uScreenResolution) * blurRadius;
    vec3 accumColor = vec3(0.0);
    float totalWeight = 0.0;

    // 16-tap disc sampling kernel
    const int SAMPLES = 16;
    for (int i = 0; i < SAMPLES; ++i) {
        float angle = float(i) * (2.0 * PI / float(SAMPLES));
        float r = sqrt(float(i + 1) / float(SAMPLES));
        vec2 sampleOffset = vec2(cos(angle), sin(angle)) * r * texelSize;
        accumColor += texture(uColorTexture, uv + sampleOffset).rgb;
        totalWeight += 1.0;
    }

    return accumColor / totalWeight;
}

// Luminance (ITU-R BT.601)
float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// Sobel gradient magnitude: G = sqrt(Gx^2 + Gy^2)
float sobelMagnitude(vec2 uv) {
    vec2 t = 1.0 / uScreenResolution;
    float tl = luma(texture(uColorTexture, uv + vec2(-t.x,  t.y)).rgb);
    float tc = luma(texture(uColorTexture, uv + vec2( 0.0,  t.y)).rgb);
    float tr = luma(texture(uColorTexture, uv + vec2( t.x,  t.y)).rgb);
    float ml = luma(texture(uColorTexture, uv + vec2(-t.x,  0.0)).rgb);
    float mr = luma(texture(uColorTexture, uv + vec2( t.x,  0.0)).rgb);
    float bl = luma(texture(uColorTexture, uv + vec2(-t.x, -t.y)).rgb);
    float bc = luma(texture(uColorTexture, uv + vec2( 0.0, -t.y)).rgb);
    float br = luma(texture(uColorTexture, uv + vec2( t.x, -t.y)).rgb);

    // Gx = [-1 0 1; -2 0 2; -1 0 1], Gy = [1 2 1; 0 0 0; -1 -2 -1]
    float gx = -tl - 2.0 * ml - bl + tr + 2.0 * mr + br;
    float gy =  tl + 2.0 * tc + tr - bl - 2.0 * bc - br;
    return sqrt(gx * gx + gy * gy);
}

// Night vision: amplified luminance mapped to green phosphor with grain
vec3 applyNightVision(vec3 color, vec2 uv) {
    float l = luma(color);
    float amplified = clamp(pow(l * 4.0, 0.7), 0.0, 1.0); // image intensifier gain
    float grain = generateGaussianNoise(uv * uScreenResolution, uTime) * 0.06;
    vec3 phosphor = vec3(0.1, 1.0, 0.2) * (amplified + grain);

    // Circular goggle mask
    vec2 p = uv * 2.0 - 1.0;
    p.x *= uScreenResolution.x / uScreenResolution.y;
    float mask = 1.0 - smoothstep(0.85, 1.0, length(p));
    return clamp(phosphor * mask, 0.0, 1.0);
}

// CCTV Crosshair and Frame Overlay
vec3 applyCCTVOverlay(vec3 color, vec2 uv) {
    vec2 p = uv * 2.0 - 1.0;
    vec3 outColor = color;

    // Vignette
    float vig = 1.0 - dot(p * 0.45, p * 0.45);
    outColor *= clamp(vig, 0.2, 1.0);

    // Subtle horizontal scanline effect
    float scanline = sin(uv.y * uScreenResolution.y * 1.5) * 0.04;
    outColor -= vec3(scanline);

    // Center crosshairs
    vec2 d = abs(p);
    if ((d.x < 0.04 && d.y < 0.002) || (d.y < 0.04 && d.x < 0.002)) {
        outColor = vec3(0.0, 1.0, 0.4); // Bright green tactical reticle
    }

    // Four corner brackets
    float cornerSize = 0.85;
    float bracketWidth = 0.08;
    float thickness = 0.003;
    if ((abs(p.x) > cornerSize && abs(p.y) > cornerSize - bracketWidth && abs(abs(p.x) - cornerSize) < thickness) ||
        (abs(p.y) > cornerSize && abs(p.x) > cornerSize - bracketWidth && abs(abs(p.y) - cornerSize) < thickness)) {
        outColor = vec3(0.9, 0.9, 0.9);
    }

    return outColor;
}

void main() {
    vec3 pristine = texture(uColorTexture, TexCoords).rgb;
    vec3 finalOutput;

    switch (uViewMode) {
        case 1: { // VIEW_PRISTINE: Direct clean 3D render
            finalOutput = pristine;
            break;
        }
        case 2: { // VIEW_CCTV_RAW: Pristine with security HUD & scanlines
            finalOutput = applyCCTVOverlay(pristine, TexCoords);
            break;
        }
        case 3: { // VIEW_DEGRADED: Low-light dampening + Gaussian sensor noise
            vec3 degraded = degradeColor(pristine, TexCoords);
            finalOutput = applyCCTVOverlay(degraded, TexCoords);
            break;
        }
        case 4: { // VIEW_ENHANCED: Denoised by spatial convolution + contrast stretched
            vec3 filtered = applyKernel(TexCoords, true);
            vec3 enhanced = enhanceContrast(filtered);
            finalOutput = applyCCTVOverlay(enhanced, TexCoords);
            break;
        }
        case 5: { // VIEW_DEPTH_MAP: Visualized view-space depth
            float rawDepth = texture(uDepthTexture, TexCoords).r;
            float linearZ = linearizeDepth(rawDepth);
            float normalizedZ = clamp((linearZ - uNearPlane) / (uFarPlane * 0.4 - uNearPlane), 0.0, 1.0);
            finalOutput = vec3(normalizedZ);
            break;
        }
        case 6: { // VIEW_DOF: Depth-Driven Depth of Field (Equation 2)
            vec3 dofColor = applyDepthOfField(TexCoords);
            finalOutput = dofColor;
            break;
        }
        case 7: { // VIEW_SPLIT_SCREEN: Comparison (Left: Degraded, Right: Enhanced DIP)
            float splitX = uSplitPosition;
            if (abs(TexCoords.x - splitX) < (2.0 / uScreenResolution.x)) {
                // Divider line in high-contrast cyan
                finalOutput = vec3(0.0, 0.9, 1.0);
            } else if (TexCoords.x < splitX) {
                // Left: Degraded Feed
                vec3 degraded = degradeColor(pristine, TexCoords);
                finalOutput = applyCCTVOverlay(degraded, TexCoords);
            } else {
                // Right: Enhanced DIP Feed
                vec3 filtered = applyKernel(TexCoords, true);
                vec3 enhanced = enhanceContrast(filtered);
                finalOutput = applyCCTVOverlay(enhanced, TexCoords);
            }
            break;
        }
        case 8: { // VIEW_EDGES: Sobel edge detection (intrusion outline)
            float g = sobelMagnitude(TexCoords);
            float edge = smoothstep(0.08, 0.4, g);
            finalOutput = mix(pristine * 0.15, vec3(1.0, 0.85, 0.1), edge);
            break;
        }
        case 9: { // VIEW_NIGHT_VISION: Green phosphor image intensifier
            finalOutput = applyNightVision(pristine, TexCoords);
            break;
        }
        case 10: { // VIEW_SIDE_BY_SIDE: original (left) vs filtered (right), same aspect ratio
            // Each half shows the whole frame at half size, centred vertically.
            float gap = 0.01;
            float panelW = 0.5 - 1.5 * gap;
            float panelH = panelW;                 // same aspect as the screen
            float y0 = 0.5 - panelH * 0.5;
            vec2 leftOrigin = vec2(gap, y0);
            vec2 rightOrigin = vec2(0.5 + 0.5 * gap, y0);
            vec2 lp = (TexCoords - leftOrigin) / vec2(panelW, panelH);
            vec2 rp = (TexCoords - rightOrigin) / vec2(panelW, panelH);
            finalOutput = vec3(0.06, 0.07, 0.10);
            if (all(greaterThanEqual(lp, vec2(0.0))) && all(lessThanEqual(lp, vec2(1.0)))) {
                finalOutput = texture(uColorTexture, lp).rgb;
            } else if (all(greaterThanEqual(rp, vec2(0.0))) && all(lessThanEqual(rp, vec2(1.0)))) {
                finalOutput = applyKernel(rp, false);
            }
            break;
        }
        default: {
            finalOutput = pristine;
            break;
        }
    }

    FragColor = vec4(finalOutput, 1.0);
}
