#include "Shader.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

Shader::Shader() : ID(0) {}

// Replaces lines of the form  #include "file.glsl"  with that file's contents
// (path relative to the including shader), so shaders can share code.
static std::string resolveIncludes(const std::string& code, const std::string& dir) {
    std::stringstream in(code), out;
    std::string line;
    while (std::getline(in, line)) {
        size_t pos = line.find("#include");
        if (pos != std::string::npos && line.find_first_not_of(" \t") == pos) {
            size_t a = line.find('"'), b = line.rfind('"');
            std::ifstream inc(dir + line.substr(a + 1, b - a - 1));
            if (!inc) {
                std::cerr << "[ERROR] Shader include not found: " << dir + line.substr(a + 1, b - a - 1) << std::endl;
                continue;
            }
            std::stringstream incCode;
            incCode << inc.rdbuf();
            out << resolveIncludes(incCode.str(), dir) << "\n";
        } else {
            out << line << "\n";
        }
    }
    return out.str();
}

static std::string directoryOf(const char* path) {
    std::string p(path);
    size_t slash = p.find_last_of("/\\");
    return slash == std::string::npos ? "" : p.substr(0, slash + 1);
}

Shader::Shader(const char* vertexPath, const char* fragmentPath) : ID(0) {
    load(vertexPath, fragmentPath);
}

Shader::~Shader() {
    if (ID != 0) {
        glDeleteProgram(ID);
    }
}

bool Shader::load(const char* vertexPath, const char* fragmentPath) {
    std::string vertexCode;
    std::string fragmentCode;
    std::ifstream vShaderFile;
    std::ifstream fShaderFile;

    vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);

    try {
        vShaderFile.open(vertexPath);
        fShaderFile.open(fragmentPath);
        std::stringstream vShaderStream, fShaderStream;

        vShaderStream << vShaderFile.rdbuf();
        fShaderStream << fShaderFile.rdbuf();

        vShaderFile.close();
        fShaderFile.close();

        vertexCode = resolveIncludes(vShaderStream.str(), directoryOf(vertexPath));
        fragmentCode = resolveIncludes(fShaderStream.str(), directoryOf(fragmentPath));
    }
    catch (std::ifstream::failure& e) {
        std::cerr << "[ERROR] Shader file not successfully read: " << vertexPath << " / " << fragmentPath 
                  << " (" << e.what() << ")" << std::endl;
        return false;
    }

    const char* vShaderCode = vertexCode.c_str();
    const char* fShaderCode = fragmentCode.c_str();

    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vShaderCode, NULL);
    glCompileShader(vertex);
    checkCompileErrors(vertex, "VERTEX");

    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fShaderCode, NULL);
    glCompileShader(fragment);
    checkCompileErrors(fragment, "FRAGMENT");

    if (ID != 0) {
        glDeleteProgram(ID);
    }
    locationCache.clear();

    ID = glCreateProgram();
    glAttachShader(ID, vertex);
    glAttachShader(ID, fragment);
    glLinkProgram(ID);
    checkCompileErrors(ID, "PROGRAM");

    glDeleteShader(vertex);
    glDeleteShader(fragment);

    return true;
}

void Shader::use() const {
    if (ID != 0) {
        glUseProgram(ID);
    }
}

// Uniform locations are looked up once per name and cached: the town issues
// thousands of draws per frame and glGetUniformLocation is a driver round trip.
GLint Shader::loc(const std::string& name) const {
    auto it = locationCache.find(name);
    if (it != locationCache.end()) return it->second;
    GLint l = glGetUniformLocation(ID, name.c_str());
    locationCache.emplace(name, l);
    return l;
}

void Shader::setBool(const std::string& name, bool value) const {
    glUniform1i(loc(name), (int)value);
}

void Shader::setInt(const std::string& name, int value) const {
    glUniform1i(loc(name), value);
}

void Shader::setFloat(const std::string& name, float value) const {
    glUniform1f(loc(name), value);
}

void Shader::setVec2(const std::string& name, const glm::vec2& value) const {
    glUniform2fv(loc(name), 1, &value[0]);
}

void Shader::setVec2(const std::string& name, float x, float y) const {
    glUniform2f(loc(name), x, y);
}

void Shader::setVec3(const std::string& name, const glm::vec3& value) const {
    glUniform3fv(loc(name), 1, &value[0]);
}

void Shader::setVec3(const std::string& name, float x, float y, float z) const {
    glUniform3f(loc(name), x, y, z);
}

void Shader::setVec4(const std::string& name, const glm::vec4& value) const {
    glUniform4fv(loc(name), 1, &value[0]);
}

void Shader::setMat4(const std::string& name, const glm::mat4& mat) const {
    glUniformMatrix4fv(loc(name), 1, GL_FALSE, &mat[0][0]);
}

void Shader::checkCompileErrors(GLuint shader, const std::string& type) {
    GLint success;
    GLchar infoLog[1024];
    if (type != "PROGRAM") {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 1024, NULL, infoLog);
            std::cerr << "[ERROR] SHADER_COMPILATION_ERROR of type: " << type << "\n" << infoLog << "\n"
                      << "---------------------------------------------------" << std::endl;
        }
    } else {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shader, 1024, NULL, infoLog);
            std::cerr << "[ERROR] PROGRAM_LINKING_ERROR of type: " << type << "\n" << infoLog << "\n"
                      << "---------------------------------------------------" << std::endl;
        }
    }
}
