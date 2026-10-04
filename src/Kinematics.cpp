#include "Kinematics.h"
#include <cmath>

CCTVKinematicChain::CCTVKinematicChain(glm::vec3 mountPos)
    : basePosition(mountPos),
      yawAngle(45.0f),
      pitchAngle(28.0f),    // positive tilts the lens down (R_x turns +Z towards -Y)
      autoSweep(true),
      sweepSpeed(18.0f),
      minYaw(10.0f),
      maxYaw(95.0f),
      sweepDirection(1.0f),
      baseHeight(0.8f),
      armLength(0.6f),
      headLength(0.9f) {}

void CCTVKinematicChain::update(float deltaTime) {
    if (autoSweep) {
        yawAngle += sweepDirection * sweepSpeed * deltaTime;
        if (yawAngle >= maxYaw) {
            yawAngle = maxYaw;
            sweepDirection = -1.0f;
        } else if (yawAngle <= minYaw) {
            yawAngle = minYaw;
            sweepDirection = 1.0f;
        }
    }
}

glm::mat4 CCTVKinematicChain::getBaseMatrix() const {
    glm::mat4 m(1.0f);
    m = glm::translate(m, basePosition);
    return m;
}

glm::mat4 CCTVKinematicChain::getYawJointMatrix() const {
    glm::mat4 m = getBaseMatrix();
    m = glm::translate(m, glm::vec3(0.0f, baseHeight * 0.5f, 0.0f));
    m = glm::rotate(m, glm::radians(yawAngle), glm::vec3(0.0f, 1.0f, 0.0f));
    return m;
}

glm::mat4 CCTVKinematicChain::getPitchJointMatrix() const {
    glm::mat4 m = getYawJointMatrix();
    m = glm::translate(m, glm::vec3(0.0f, 0.2f, 0.3f));
    m = glm::rotate(m, glm::radians(pitchAngle), glm::vec3(1.0f, 0.0f, 0.0f));
    return m;
}

glm::mat4 CCTVKinematicChain::getLensMatrix() const {
    glm::mat4 m = getPitchJointMatrix();
    m = glm::translate(m, glm::vec3(0.0f, 0.0f, headLength * 0.5f));
    return m;
}

glm::vec3 CCTVKinematicChain::getLensWorldPosition() const {
    glm::mat4 lensMat = getLensMatrix();
    return glm::vec3(lensMat[3]);
}

glm::vec3 CCTVKinematicChain::getLensForwardDirection() const {
    glm::mat4 pitchMat = getPitchJointMatrix();
    // Forward direction is local +Z transformed by yaw and pitch rotations
    return glm::normalize(glm::vec3(pitchMat * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
}

glm::vec3 CCTVKinematicChain::getLensUpVector() const {
    glm::mat4 pitchMat = getPitchJointMatrix();
    return glm::normalize(glm::vec3(pitchMat * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
}

glm::mat4 CCTVKinematicChain::getCCTVViewMatrix() const {
    glm::vec3 forward = getLensForwardDirection();
    glm::vec3 eye = getLensWorldPosition() + forward * 0.45f;   // just in front of the lens glass, not inside the housing
    glm::vec3 up = getLensUpVector();
    return glm::lookAt(eye, eye + forward, up);
}
