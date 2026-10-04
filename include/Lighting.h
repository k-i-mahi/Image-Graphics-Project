#pragma once

#include <glm/glm.hpp>
#include <string>

enum ShadingModel {
    SHADING_PHONG = 0,
    SHADING_GOURAUD = 1,
    SHADING_FLAT = 2
};

struct DirLight {
    glm::vec3 direction;
    glm::vec3 ambient;
    glm::vec3 diffuse;
    glm::vec3 specular;
    bool enabled;
};

struct PointLight {
    glm::vec3 position;
    glm::vec3 ambient;
    glm::vec3 diffuse;
    glm::vec3 specular;
    
    // Attenuation coefficients: 1.0 / (constant + linear * d + quadratic * d^2)
    float constant;
    float linear;
    float quadratic;
    bool enabled;
};

struct SpotLight {
    glm::vec3 position;
    glm::vec3 direction;
    glm::vec3 ambient;
    glm::vec3 diffuse;
    glm::vec3 specular;
    
    float cutOff;       // cosine of inner angle
    float outerCutOff;  // cosine of outer angle
    
    float constant;
    float linear;
    float quadratic;
    bool enabled;
};

struct Material {
    glm::vec3 ambient;
    glm::vec3 diffuse;
    glm::vec3 specular;
    float shininess;
};
