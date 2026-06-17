#include "renderer/Shader.h"
#include "debug/Log.h"
#include "debug/OpenGL.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

// ── File reading ──────────────────────────────────────────────────────

std::string Shader::readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// ── Shader compilation ────────────────────────────────────────────────

GLuint Shader::compileShader(GLenum type, const std::string& source) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::string typeName = (type == GL_VERTEX_SHADER) ? "vertex" : "fragment";
        LOG_ERROR("Shader compilation error (" << typeName << "):\n" << infoLog);
        glDeleteShader(shader);
        throw std::runtime_error("Shader compilation failed");
    }
    return shader;
}

// ── Constructor / Destructor ──────────────────────────────────────────

Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath) {
    std::string vertexSrc   = readFile(vertexPath);
    std::string fragmentSrc = readFile(fragmentPath);

    GLuint vs = compileShader(GL_VERTEX_SHADER,   vertexSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSrc);

    m_programID = glCreateProgram();
    glAttachShader(m_programID, vs);
    glAttachShader(m_programID, fs);
    glLinkProgram(m_programID);

    GLint success;
    glGetProgramiv(m_programID, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(m_programID, 512, nullptr, infoLog);
        LOG_ERROR("Shader link error:\n" << infoLog);
        glDeleteShader(vs);
        glDeleteShader(fs);
        glDeleteProgram(m_programID);
        throw std::runtime_error("Shader linking failed");
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
}

Shader::~Shader() {
    if (m_programID != 0) {
        glDeleteProgram(m_programID);
    }
}

Shader::Shader(Shader&& other) noexcept : m_programID(other.m_programID) {
    other.m_programID = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (m_programID != 0) glDeleteProgram(m_programID);
        m_programID = other.m_programID;
        other.m_programID = 0;
    }
    return *this;
}

// ── Methods ───────────────────────────────────────────────────────────

void Shader::bind() const {
    glUseProgram(m_programID);
}

void Shader::unbind() const {
    glUseProgram(0);
}

GLint Shader::getUniformLocation(const std::string& name) const {
    auto it = m_uniformCache.find(name);
    if (it != m_uniformCache.end()) {
        return it->second;
    }
    GLint loc = glGetUniformLocation(m_programID, name.c_str());
    m_uniformCache[name] = loc;
    return loc;
}

void Shader::setMat4(const std::string& name, const glm::mat4& mat) const {
    GLint loc = getUniformLocation(name);
    glUniformMatrix4fv(loc, 1, GL_FALSE, &mat[0][0]);
}

void Shader::setVec3(const std::string& name, const glm::vec3& vec) const {
    GLint loc = getUniformLocation(name);
    glUniform3f(loc, vec.x, vec.y, vec.z);
}

void Shader::setVec4(const std::string& name, const glm::vec4& vec) const {
    GLint loc = getUniformLocation(name);
    glUniform4f(loc, vec.x, vec.y, vec.z, vec.w);
}

void Shader::setInt(const std::string& name, int value) const {
    GLint loc = getUniformLocation(name);
    glUniform1i(loc, value);
}
