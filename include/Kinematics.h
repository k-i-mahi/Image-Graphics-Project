#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class CCTVKinematicChain {
public:
    // Base mount location in world space
    glm::vec3 basePosition;
    
    // Joint angles in degrees
    float yawAngle;       // Rotation about Y axis (pan)
    float pitchAngle;     // Rotation about local X axis (tilt)
    
    // Automatic panning controls
    bool autoSweep;
    float sweepSpeed;
    float minYaw;
    float maxYaw;
    float sweepDirection;

    // Dimensions
    float baseHeight;
    float armLength;
    float headLength;

    CCTVKinematicChain(glm::vec3 mountPos = glm::vec3(-21.0f, 13.5f, -21.0f));

    void update(float deltaTime);

    // Hierarchical transformation matrices for rendering geometry
    glm::mat4 getBaseMatrix() const;
    glm::mat4 getYawJointMatrix() const;
    glm::mat4 getPitchJointMatrix() const;
    glm::mat4 getLensMatrix() const;

    // World space position and direction of the camera lens / optical axis
    glm::vec3 getLensWorldPosition() const;
    glm::vec3 getLensForwardDirection() const;
    glm::vec3 getLensUpVector() const;

    // View matrix from the perspective of the CCTV camera lens
    glm::mat4 getCCTVViewMatrix() const;
};
