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
    Mesh(const Mesh&) = delete;              // owns GL objects: not copyable
    Mesh& operator=(const Mesh&) = delete;

    void setupMesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices = {});
    void draw() const;
};

class GeometryManager {
public:
    // CPU copies of the unit meshes: 0 cube, 1 cylinder, 2 sphere, 3 prism, 4 cone (see MeshKind in Town.h)
    struct CpuMesh { std::vector<Vertex> vertices; std::vector<unsigned int> indices; };
    CpuMesh cpu[5];

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
    void keepCpu(int kind, const std::vector<Vertex>& v, const std::vector<unsigned int>& idx);
    void generateCube();
    void generateCylinder(int sectors = 32);
    void generateSphere(int rings = 20, int sectors = 32);
    void generateQuad();
    void generateGround(float size = 80.0f, int divisions = 40);
    void generatePrism();
    void generateCone(int sectors = 24);
};
