#pragma once

#include <glm/glm.hpp>

// Cubic Bezier segment, used for every turn of the traffic and pedestrian routes.
//   P(t)  = (1-t)^3 P0 + 3(1-t)^2 t P1 + 3(1-t) t^2 P2 + t^3 P3
//   P'(t) = 3(1-t)^2 (P1-P0) + 6(1-t) t (P2-P1) + 3 t^2 (P3-P2)   (tangent -> vehicle heading)
struct BezierSegment {
    glm::vec3 p0, p1, p2, p3;

    BezierSegment(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d)
        : p0(a), p1(b), p2(c), p3(d) {}

    glm::vec3 evaluate(float t) const {
        float u = 1.0f - t;
        float tt = t * t;
        float uu = u * u;
        float uuu = uu * u;
        float ttt = tt * t;

        return (uuu * p0) + (3.0f * uu * t * p1) + (3.0f * u * tt * p2) + (ttt * p3);
    }

    glm::vec3 evaluateDerivative(float t) const {
        float u = 1.0f - t;
        return 3.0f * u * u * (p1 - p0) + 6.0f * u * t * (p2 - p1) + 3.0f * t * t * (p3 - p2);
    }
};
