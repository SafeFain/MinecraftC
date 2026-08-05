#include "renderer/Shader.h"
#include "debug/Log.h"
#include "renderer/backend/opengl/OpenGLDebug.h"
#include "core/AssetStore.h"
#include "renderer/ShaderDialect.h"

#include <stdexcept>
#include <unordered_map>

struct Shader::Impl {
    GLuint program = 0;
    mutable std::unordered_map<std::string, GLint> uniformCache;
};

// ── File reading ──────────────────────────────────────────────────────

std::string Shader::readFile(const std::filesystem::path& path) {
    return AssetStore::readTextPath(path);
}

// ── Shader compilation ────────────────────────────────────────────────

static GLuint compileShader(GLenum type, const std::string& source) {
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

Shader::Shader(const std::filesystem::path& vertexPath,
               const std::filesystem::path& fragmentPath, GraphicsApi api)
    : m_impl(std::make_unique<Impl>()) {
    std::string vertexSrc   = sourceForApi(readFile(vertexPath), api);
    std::string fragmentSrc = sourceForApi(readFile(fragmentPath), api);

    GLuint vs = compileShader(GL_VERTEX_SHADER,   vertexSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSrc);

    m_impl->program = glCreateProgram();
    glAttachShader(m_impl->program, vs);
    glAttachShader(m_impl->program, fs);
    glLinkProgram(m_impl->program);

    GLint success;
    glGetProgramiv(m_impl->program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(m_impl->program, 512, nullptr, infoLog);
        LOG_ERROR("Shader link error:\n" << infoLog);
        glDeleteShader(vs);
        glDeleteShader(fs);
        glDeleteProgram(m_impl->program);
        throw std::runtime_error("Shader linking failed");
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
}

std::string Shader::sourceForApi(std::string source, GraphicsApi api) {
    return shaderSourceForApi(std::move(source), api);
}

Shader::~Shader() {
    if (m_impl && m_impl->program != 0) {
        glDeleteProgram(m_impl->program);
    }
}

Shader::Shader(Shader&& other) noexcept = default;

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (m_impl && m_impl->program != 0) glDeleteProgram(m_impl->program);
        m_impl = std::move(other.m_impl);
    }
    return *this;
}

// ── Methods ───────────────────────────────────────────────────────────

void Shader::bind() const {
    glUseProgram(m_impl->program);
}

void Shader::unbind() const {
    glUseProgram(0);
}

int Shader::getUniformLocation(const std::string& name) const {
    auto it = m_impl->uniformCache.find(name);
    if (it != m_impl->uniformCache.end()) {
        return it->second;
    }
    GLint loc = glGetUniformLocation(m_impl->program, name.c_str());
    m_impl->uniformCache[name] = loc;
    return loc;
}

void Shader::setMat4(const std::string& name, const glm::mat4& mat) const {
    GLint loc = getUniformLocation(name);
    glUniformMatrix4fv(loc, 1, GL_FALSE, &mat[0][0]);
}

void Shader::setMat4Array(const std::string& name, const glm::mat4* matrices,
                          std::size_t count) const {
    if (count == 0 || matrices == nullptr) return;
    const GLint loc = getUniformLocation(name);
    glUniformMatrix4fv(loc, static_cast<GLsizei>(count), GL_FALSE,
                       &matrices[0][0][0]);
}

void Shader::setVec3(const std::string& name, const glm::vec3& vec) const {
    GLint loc = getUniformLocation(name);
    glUniform3f(loc, vec.x, vec.y, vec.z);
}

void Shader::setVec4(const std::string& name, const glm::vec4& vec) const {
    GLint loc = getUniformLocation(name);
    glUniform4f(loc, vec.x, vec.y, vec.z, vec.w);
}

void Shader::setFloat(const std::string& name, float value) const {
    GLint loc = getUniformLocation(name);
    glUniform1f(loc, value);
}

void Shader::setInt(const std::string& name, int value) const {
    GLint loc = getUniformLocation(name);
    glUniform1i(loc, value);
}
