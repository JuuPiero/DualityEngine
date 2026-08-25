#include "DualityEngine/Renderer/OpenGL/GLShaderProgram.h"

#include <cstdio>

#include <GL/glew.h>

namespace Duality {

    namespace {
        unsigned int CompileShader(unsigned int type, const char* source) {
            unsigned int shader = glCreateShader(type);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);

            int success = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (!success) {
                char log[512];
                glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
                std::fprintf(stderr, "GLShaderProgram: shader compile error: %s\n", log);
            }
            return shader;
        }
    }

    void GLShaderProgram::Init(const char* vertexSource, const char* fragmentSource) {
        unsigned int vertexShader = CompileShader(GL_VERTEX_SHADER, vertexSource);
        unsigned int fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);

        m_Program = glCreateProgram();
        glAttachShader(m_Program, vertexShader);
        glAttachShader(m_Program, fragmentShader);
        glLinkProgram(m_Program);

        int success = 0;
        glGetProgramiv(m_Program, GL_LINK_STATUS, &success);
        if (!success) {
            char log[512];
            glGetProgramInfoLog(m_Program, sizeof(log), nullptr, log);
            std::fprintf(stderr, "GLShaderProgram: shader link error: %s\n", log);
        }

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
    }

    void GLShaderProgram::Shutdown() {
        glDeleteProgram(m_Program);
        m_Program = 0;
    }

    void GLShaderProgram::Bind() const {
        glUseProgram(m_Program);
    }

    void GLShaderProgram::Unbind() const {
        glUseProgram(0);
    }

    int GLShaderProgram::GetUniformLocation(const char* name) const {
        return glGetUniformLocation(m_Program, name);
    }

    int GLShaderProgram::GetAttribLocation(const char* name) const {
        return glGetAttribLocation(m_Program, name);
    }

    void GLShaderProgram::SetUniformMat4(int location, const glm::mat4& value) const {
        glUniformMatrix4fv(location, 1, GL_FALSE, &value[0][0]);
    }

    void GLShaderProgram::SetUniformVec4(int location, const glm::vec4& value) const {
        glUniform4f(location, value.r, value.g, value.b, value.a);
    }

    void GLShaderProgram::SetUniformInt(int location, int value) const {
        glUniform1i(location, value);
    }

}
