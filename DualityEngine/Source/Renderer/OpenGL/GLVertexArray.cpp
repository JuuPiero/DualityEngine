#include "DualityEngine/Renderer/OpenGL/GLVertexArray.h"

#include <GL/glew.h>

namespace Duality {

    void GLVertexArray::Init() {
        glGenVertexArrays(1, &m_Vao);
        glGenBuffers(1, &m_Vbo);
    }

    void GLVertexArray::Shutdown() {
        glDeleteBuffers(1, &m_Vbo);
        glDeleteVertexArrays(1, &m_Vao);
        m_Vao = 0;
        m_Vbo = 0;
        m_VertexCount = 0;
    }

    void GLVertexArray::Bind() const {
        glBindVertexArray(m_Vao);
    }

    void GLVertexArray::Unbind() const {
        glBindVertexArray(0);
    }

    void GLVertexArray::SetVertexData(const void* data, size_t sizeBytes, int vertexCount) {
        glBindBuffer(GL_ARRAY_BUFFER, m_Vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeBytes), data, GL_STATIC_DRAW);
        m_VertexCount = vertexCount;
    }

    void GLVertexArray::AddFloatAttribute(int location, int componentCount, size_t stride, size_t offset) {
        glEnableVertexAttribArray(static_cast<GLuint>(location));
        glVertexAttribPointer(static_cast<GLuint>(location), componentCount, GL_FLOAT, GL_FALSE,
            static_cast<GLsizei>(stride), reinterpret_cast<void*>(offset));
    }

}
