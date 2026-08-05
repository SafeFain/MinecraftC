#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <glm/glm.hpp>

#include "core/GraphicsApi.h"

class Shader {
public:
    Shader(const std::filesystem::path& vertexPath,
           const std::filesystem::path& fragmentPath,
           GraphicsApi api = GraphicsApi::OpenGL33);
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    void bind() const;
    void unbind() const;

    void setMat4(const std::string& name, const glm::mat4& mat) const;
    void setMat4Array(const std::string& name, const glm::mat4* matrices,
                      std::size_t count) const;
    void setVec3(const std::string& name, const glm::vec3& vec) const;
    void setVec4(const std::string& name, const glm::vec4& vec) const;
    void setFloat(const std::string& name, float value) const;
    void setInt(const std::string& name, int value) const;

    static std::string sourceForApi(std::string source, GraphicsApi api);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    int getUniformLocation(const std::string& name) const;
    static std::string readFile(const std::filesystem::path& path);
};
