#include "world/ChunkMesh.h"
#include "debug/OpenGL.h"
#include <cstddef>

void ChunkMesh::upload() {
    // Destroy old GPU resources
    destroy();

    if (vertices.empty() || indices.empty()) {
        gpuReady = false;
        indexCount = 0;
        return;
    }

    // Verify MeshVertex layout at compile time
    static_assert(sizeof(MeshVertex) == 44, "MeshVertex must be 11 tightly-packed floats");
    static_assert(offsetof(MeshVertex, px) == 0,  "px at offset 0");
    static_assert(offsetof(MeshVertex, ao) == 12, "ao at offset 12");

    GLuint vbo, ebo;
    GL_CHECK(glGenVertexArrays(1, &vao));
    GL_CHECK(glBindVertexArray(vao));

    // Single interleaved VBO with lighting and material attributes.
    GL_CHECK(glGenBuffers(1, &vbo));
    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, vbo));
    GL_CHECK(glBufferData(GL_ARRAY_BUFFER,
                 vertices.size() * sizeof(MeshVertex),
                 vertices.data(), GL_STATIC_DRAW));

    // Position: location=0, 3 floats, stride=sizeof(MeshVertex), offset=0
    GL_CHECK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                          reinterpret_cast<void*>(0)));
    GL_CHECK(glEnableVertexAttribArray(0));

    // AO, sky light, reserved, alpha.
    GL_CHECK(glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                          reinterpret_cast<void*>(12)));
    GL_CHECK(glEnableVertexAttribArray(1));

    // Repeating tile UV and atlas tile index.
    GL_CHECK(glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                          reinterpret_cast<void*>(28)));
    GL_CHECK(glEnableVertexAttribArray(2));

    GL_CHECK(glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                          reinterpret_cast<void*>(40)));
    GL_CHECK(glEnableVertexAttribArray(3));

    // Element buffer
    GL_CHECK(glGenBuffers(1, &ebo));
    GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo));
    GL_CHECK(glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 indices.size() * sizeof(unsigned int),
                 indices.data(), GL_STATIC_DRAW));

    GL_CHECK(glBindVertexArray(0));
    GL_CHECK(glDeleteBuffers(1, &vbo));  // VAO holds reference, name can be freed
    // Note: EBO is NOT deleted here — it's attached to the VAO

    indexCount = indices.size();
    gpuReady = true;
}

void ChunkMesh::destroy() {
    if (vao != 0) {
        GL_CHECK(glDeleteVertexArrays(1, &vao));
        vao = 0;
    }
    gpuReady = false;
    indexCount = 0;
}
