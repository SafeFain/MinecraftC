#pragma once

#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

#include <glad/glad.h>

class Shader {
public:
    Shader(const std::string& vertexPath, const std::string& fragmentPath);
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    void bind() const;
    void unbind() const;

    void setMat4(const std::string& name, const glm::mat4& mat) const;
    void setVec3(const std::string& name, const glm::vec3& vec) const;
    void setVec4(const std::string& name, const glm::vec4& vec) const;
    void setInt(const std::string& name, int value) const;

    GLuint id() const { return m_programID; }

private:
    GLuint m_programID = 0;
    mutable std::unordered_map<std::string, GLint> m_uniformCache;

    GLint getUniformLocation(const std::string& name) const;

    static GLuint compileShader(GLenum type, const std::string& source);
    static std::string readFile(const std::string& path);
};
