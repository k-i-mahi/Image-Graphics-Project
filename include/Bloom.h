#pragma once

#include <glad/glad.h>
#include "Shader.h"
#include "Geometry.h"

// Bloom: glow around lamps, lit windows, headlights and signals.
//   1. bright pass at half resolution, weighted by the glow mask in the scene's alpha
//   2. separable Gaussian blur (horizontal + vertical), repeated with a growing spread
// The DIP shader adds the result to the camera image (screen blend).
class Bloom {
public:
    bool enabled = true;
    float strength = 1.3f;

    bool init(int width, int height);
    void resize(int width, int height);
    // Returns the blurred glow texture (half resolution). Leaves FBO 0 bound; the caller restores the viewport.
    GLuint run(const GeometryManager& geo, GLuint sceneColor);

private:
    Shader bright, blur;
    GLuint fbo[2] = { 0, 0 }, tex[2] = { 0, 0 };
    int w = 0, h = 0, fullW = 0, fullH = 0;
    void allocate();
};
