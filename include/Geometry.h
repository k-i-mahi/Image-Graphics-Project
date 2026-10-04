#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

struct Vertex {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
};

class Mesh {
public:
    GLuint VAO, VBO, EBO;
    GLsizei indexCount;
    GLsizei vertexCount;
    bool hasIndices;

    Mesh();
    ~Mesh();

    void setupMesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices = {});
    void draw() const;
};

class GeometryManager {
public:
    Mesh cubeMesh;
    Mesh cylinderMesh;
    Mesh sphereMesh;
    Mesh quadMesh;
    Mesh groundMesh;
    Mesh prismMesh;   // triangular prism (gable roof): ridge along z, apex at y = +0.5
    Mesh coneMesh;    // cone: base radius 0.5 at y = -0.5, apex at y = +0.5

    GeometryManager();
    ~GeometryManager();

    void init();

    void drawCube() const;
    void drawCylinder() const;
    void drawSphere() const;
    void drawQuad() const;
    void drawGround() const;
    void drawPrism() const;
    void drawCone() const;

private:
    void generateCube();
    void generateCylinder(int sectors = 32);
    void generateSphere(int rings = 20, int sectors = 32);
    void generateQuad();
    void generateGround(float size = 80.0f, int divisions = 40);
    void generatePrism();
    void generateCone(int sectors = 24);
};
