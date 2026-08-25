#pragma once

#include <glm/glm.hpp>

namespace Duality {

    // Compile+link+uniform/attrib-location lookup for a desktop GL shader, extracted out of
    // OpenGLRenderer3D so this boilerplate isn't reinvented the moment a second shader exists
    // (e.g. a future lit material -- see ROADMAP.md). Explicit Init()/Shutdown() rather than a
    // constructor/destructor, matching every other renderer-owned GL resource in this codebase
    // (IRenderer2D/3D, GLVertexArray) -- makes it safe to hold by value in a container without
    // worrying about copy/move semantics doing something GL-unsafe.
    //
    // Deliberately does NOT cache uniform locations by name internally (unlike a typical engine
    // uniform-cache) -- callers look a location up once (GetUniformLocation) and keep the int,
    // exactly the pattern OpenGLRenderer3D already used before this class existed, so this
    // refactor changes zero per-draw-call behavior or performance characteristics.
    class GLShaderProgram {
    public:
        // Compiles `vertexSource`/`fragmentSource`, links them into this program, and deletes
        // the intermediate shader objects (they're not needed after linking). Compile/link
        // errors are reported via stderr, matching this code's own pre-extraction convention
        // (this runs before the engine's Log:: subsystem is necessarily useful to rely on).
        void Init(const char* vertexSource, const char* fragmentSource);
        void Shutdown();

        void Bind() const;
        void Unbind() const;

        int GetUniformLocation(const char* name) const;
        int GetAttribLocation(const char* name) const;

        void SetUniformMat4(int location, const glm::mat4& value) const;
        void SetUniformVec4(int location, const glm::vec4& value) const;
        void SetUniformInt(int location, int value) const;

    private:
        unsigned int m_Program = 0;
    };

}
