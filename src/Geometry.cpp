#include "Geometry.h"
#include "PerfStats.h"
#include <cmath>
#include <glm/gtc/constants.hpp>

Mesh::Mesh() : VAO(0), VBO(0), EBO(0), indexCount(0), vertexCount(0), hasIndices(false) {}

Mesh::~Mesh() {
    if (VAO != 0) glDeleteVertexArrays(1, &VAO);
    if (VBO != 0) glDeleteBuffers(1, &VBO);
    if (EBO != 0) glDeleteBuffers(1, &EBO);
}

void Mesh::setupMesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices) {
    if (VAO != 0) glDeleteVertexArrays(1, &VAO);
    if (VBO != 0) glDeleteBuffers(1, &VBO);
    if (EBO != 0) glDeleteBuffers(1, &EBO);

    vertexCount = static_cast<GLsizei>(vertices.size());
    hasIndices = !indices.empty();
    indexCount = static_cast<GLsizei>(indices.size());

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    if (hasIndices) {
        glGenBuffers(1, &EBO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    }

    // Position attribute
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);

    // Normal attribute
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));

    // TexCoords attribute
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));

    glBindVertexArray(0);
}

void Mesh::draw() const {
    ++g_drawCalls;
    glBindVertexArray(VAO);
    if (hasIndices) {
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    }
    glBindVertexArray(0);
}

GeometryManager::GeometryManager() {}

GeometryManager::~GeometryManager() {}

void GeometryManager::init() {
    generateCube();
    generateCylinder(32);
    generateSphere(24, 32);
    generateQuad();
    generateGround(600.0f, 120);
    generatePrism();
    generateCone(24);
}

void GeometryManager::drawCube() const { cubeMesh.draw(); }
void GeometryManager::drawCylinder() const { cylinderMesh.draw(); }
void GeometryManager::drawSphere() const { sphereMesh.draw(); }
void GeometryManager::drawQuad() const { quadMesh.draw(); }
void GeometryManager::drawGround() const { groundMesh.draw(); }
void GeometryManager::drawPrism() const { prismMesh.draw(); }
void GeometryManager::drawCone() const { coneMesh.draw(); }

void GeometryManager::generateCube() {
    // 36 vertices with distinct face normals
    float vertices[] = {
        // Back face
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 0.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 1.0f,
         0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 0.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 0.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 1.0f,

        // Front face
        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 1.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.0f,

        // Left face
        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
        -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 0.0f,
        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 0.0f,

        // Right face
         0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
         0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
         0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 0.0f,

        // Bottom face
        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 1.0f,
         0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 1.0f,
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 0.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 0.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 1.0f,

        // Top face
        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 1.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 1.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 0.0f
    };

    std::vector<Vertex> vList;
    for (int i = 0; i < 36; ++i) {
        int idx = i * 8;
        Vertex v;
        v.Position = glm::vec3(vertices[idx], vertices[idx + 1], vertices[idx + 2]);
        v.Normal = glm::vec3(vertices[idx + 3], vertices[idx + 4], vertices[idx + 5]);
        v.TexCoords = glm::vec2(vertices[idx + 6], vertices[idx + 7]);
        vList.push_back(v);
    }
    cubeMesh.setupMesh(vList);
}

void GeometryManager::generateCylinder(int sectors) {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    float height = 1.0f;
    float radius = 0.5f;

    // Side vertices
    for (int i = 0; i <= sectors; ++i) {
        float angle = 2.0f * glm::pi<float>() * (float)i / (float)sectors;
        float x = radius * cos(angle);
        float z = radius * sin(angle);
        float u = (float)i / (float)sectors;

        glm::vec3 norm = glm::normalize(glm::vec3(x, 0.0f, z));

        // Bottom vertex
        Vertex vBottom;
        vBottom.Position = glm::vec3(x, -height * 0.5f, z);
        vBottom.Normal = norm;
        vBottom.TexCoords = glm::vec2(u, 0.0f);
        vertices.push_back(vBottom);

        // Top vertex
        Vertex vTop;
        vTop.Position = glm::vec3(x, height * 0.5f, z);
        vTop.Normal = norm;
        vTop.TexCoords = glm::vec2(u, 1.0f);
        vertices.push_back(vTop);
    }

    for (int i = 0; i < sectors; ++i) {
        unsigned int b1 = i * 2;
        unsigned int t1 = b1 + 1;
        unsigned int b2 = (i + 1) * 2;
        unsigned int t2 = b2 + 1;

        indices.push_back(b1);
        indices.push_back(t1);
        indices.push_back(b2);

        indices.push_back(b2);
        indices.push_back(t1);
        indices.push_back(t2);
    }

    // Top cap
    unsigned int topCenterIdx = static_cast<unsigned int>(vertices.size());
    Vertex vTopCenter;
    vTopCenter.Position = glm::vec3(0.0f, height * 0.5f, 0.0f);
    vTopCenter.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
    vTopCenter.TexCoords = glm::vec2(0.5f, 0.5f);
    vertices.push_back(vTopCenter);

    for (int i = 0; i <= sectors; ++i) {
        float angle = 2.0f * glm::pi<float>() * (float)i / (float)sectors;
        float x = radius * cos(angle);
        float z = radius * sin(angle);

        Vertex v;
        v.Position = glm::vec3(x, height * 0.5f, z);
        v.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
        v.TexCoords = glm::vec2(0.5f + 0.5f * cos(angle), 0.5f + 0.5f * sin(angle));
        vertices.push_back(v);
    }

    for (int i = 0; i < sectors; ++i) {
        indices.push_back(topCenterIdx);
        indices.push_back(topCenterIdx + 1 + i);
        indices.push_back(topCenterIdx + 2 + i);
    }

    // Bottom cap
    unsigned int botCenterIdx = static_cast<unsigned int>(vertices.size());
    Vertex vBotCenter;
    vBotCenter.Position = glm::vec3(0.0f, -height * 0.5f, 0.0f);
    vBotCenter.Normal = glm::vec3(0.0f, -1.0f, 0.0f);
    vBotCenter.TexCoords = glm::vec2(0.5f, 0.5f);
    vertices.push_back(vBotCenter);

    for (int i = 0; i <= sectors; ++i) {
        float angle = 2.0f * glm::pi<float>() * (float)i / (float)sectors;
        float x = radius * cos(angle);
        float z = radius * sin(angle);

        Vertex v;
        v.Position = glm::vec3(x, -height * 0.5f, z);
        v.Normal = glm::vec3(0.0f, -1.0f, 0.0f);
        v.TexCoords = glm::vec2(0.5f + 0.5f * cos(angle), 0.5f + 0.5f * sin(angle));
        vertices.push_back(v);
    }

    for (int i = 0; i < sectors; ++i) {
        indices.push_back(botCenterIdx);
        indices.push_back(botCenterIdx + 2 + i);
        indices.push_back(botCenterIdx + 1 + i);
    }

    cylinderMesh.setupMesh(vertices, indices);
}

void GeometryManager::generateSphere(int rings, int sectors) {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    float radius = 0.5f;

    for (int r = 0; r <= rings; ++r) {
        float phi = glm::pi<float>() * (float)r / (float)rings;
        float y = radius * cos(phi);

        for (int s = 0; s <= sectors; ++s) {
            float theta = 2.0f * glm::pi<float>() * (float)s / (float)sectors;
            float x = radius * sin(phi) * cos(theta);
            float z = radius * sin(phi) * sin(theta);

            Vertex v;
            v.Position = glm::vec3(x, y, z);
            v.Normal = glm::normalize(v.Position);
            v.TexCoords = glm::vec2((float)s / (float)sectors, (float)r / (float)rings);
            vertices.push_back(v);
        }
    }

    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < sectors; ++s) {
            unsigned int cur = r * (sectors + 1) + s;
            unsigned int next = cur + sectors + 1;

            indices.push_back(cur);
            indices.push_back(next);
            indices.push_back(cur + 1);

            indices.push_back(cur + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }

    sphereMesh.setupMesh(vertices, indices);
}

void GeometryManager::generateQuad() {
    std::vector<Vertex> vertices = {
        { glm::vec3(-1.0f,  1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(0.0f, 1.0f) },
        { glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(0.0f, 0.0f) },
        { glm::vec3( 1.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(1.0f, 0.0f) },

        { glm::vec3(-1.0f,  1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(0.0f, 1.0f) },
        { glm::vec3( 1.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(1.0f, 0.0f) },
        { glm::vec3( 1.0f,  1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(1.0f, 1.0f) }
    };
    quadMesh.setupMesh(vertices);
}

void GeometryManager::generateGround(float size, int divisions) {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    float half = size * 0.5f;
    float step = size / (float)divisions;

    for (int z = 0; z <= divisions; ++z) {
        float posZ = -half + z * step;
        for (int x = 0; x <= divisions; ++x) {
            float posX = -half + x * step;

            Vertex v;
            v.Position = glm::vec3(posX, 0.0f, posZ);
            v.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
            v.TexCoords = glm::vec2((float)x, (float)z);
            vertices.push_back(v);
        }
    }

    for (int z = 0; z < divisions; ++z) {
        for (int x = 0; x < divisions; ++x) {
            unsigned int cur = z * (divisions + 1) + x;
            unsigned int next = cur + divisions + 1;

            indices.push_back(cur);
            indices.push_back(next);
            indices.push_back(cur + 1);

            indices.push_back(cur + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }

    groundMesh.setupMesh(vertices, indices);
}

// Flat-shaded triangle helper: one face normal for all three corners
static void pushTri(std::vector<Vertex>& v, glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
    v.push_back({ a, n, glm::vec2(0.0f, 0.0f) });
    v.push_back({ b, n, glm::vec2(1.0f, 0.0f) });
    v.push_back({ c, n, glm::vec2(0.5f, 1.0f) });
}

void GeometryManager::generatePrism() {
    // Cross-section in x-y: (-0.5,-0.5) (0.5,-0.5) (0,0.5); extruded along z in [-0.5, 0.5]
    glm::vec3 l0(-0.5f, -0.5f, 0.5f), r0(0.5f, -0.5f, 0.5f), t0(0.0f, 0.5f, 0.5f);
    glm::vec3 l1(-0.5f, -0.5f, -0.5f), r1(0.5f, -0.5f, -0.5f), t1(0.0f, 0.5f, -0.5f);
    std::vector<Vertex> v;
    pushTri(v, l0, r0, t0);                       // front gable
    pushTri(v, r1, l1, t1);                       // back gable
    pushTri(v, r0, r1, t1); pushTri(v, r0, t1, t0); // right roof slope
    pushTri(v, l1, l0, t0); pushTri(v, l1, t0, t1); // left roof slope
    pushTri(v, l1, r1, r0); pushTri(v, l1, r0, l0); // bottom
    prismMesh.setupMesh(v);
}

void GeometryManager::generateCone(int sectors) {
    std::vector<Vertex> v;
    glm::vec3 apex(0.0f, 0.5f, 0.0f), base(0.0f, -0.5f, 0.0f);
    for (int i = 0; i < sectors; ++i) {
        float a0 = 2.0f * glm::pi<float>() * i / sectors;
        float a1 = 2.0f * glm::pi<float>() * (i + 1) / sectors;
        glm::vec3 p0(0.5f * std::cos(a0), -0.5f, 0.5f * std::sin(a0));
        glm::vec3 p1(0.5f * std::cos(a1), -0.5f, 0.5f * std::sin(a1));
        // Smooth side normals: slope of a cone with radius 0.5 and height 1 -> (cos, 0.5, sin)
        glm::vec3 n0 = glm::normalize(glm::vec3(std::cos(a0), 0.5f, std::sin(a0)));
        glm::vec3 n1 = glm::normalize(glm::vec3(std::cos(a1), 0.5f, std::sin(a1)));
        glm::vec3 na = glm::normalize(n0 + n1);
        v.push_back({ p0, n0, glm::vec2(0.0f) });
        v.push_back({ apex, na, glm::vec2(0.5f, 1.0f) });
        v.push_back({ p1, n1, glm::vec2(1.0f, 0.0f) });
        pushTri(v, base, p0, p1);
    }
    coneMesh.setupMesh(v);
}
