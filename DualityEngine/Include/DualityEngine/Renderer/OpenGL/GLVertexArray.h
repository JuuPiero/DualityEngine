#pragma once

#include <cstddef>
#include <vector>

#include "DualityEngine/Asset/MeshLoader.h"

namespace Duality {

    // A VAO + single VBO, extracted out of OpenGLRenderer3D's old PrimitiveGpuMesh struct (which
    // was exactly this shape already: a Vao/Vbo pair + vertex count) so the same wrapper can back
    // a second vertex-layout consumer later without re-deriving this boilerplate. Single-VBO,
    // no index buffer -- nothing in this engine uses glDrawElements yet, so an EBO would be
    // speculative; add one when a real consumer needs it.
    //
    // Explicit Init()/Shutdown() rather than a constructor/destructor -- this is stored by value
    // in OpenGLRenderer3D's own fixed array and std::vector (m_Meshes/m_ImportedMeshes), and a
    // vector growing/reallocating moves its elements; a naive RAII destructor would double-free
    // on every such move. Same reasoning as GLShaderProgram.
    class GLVertexArray {
    public:
        void Init();
        void Shutdown();

        void Bind() const;
        void Unbind() const;

        // Uploads `vertexCount` vertices (`sizeBytes` total) into this VAO's VBO as
        // GL_STATIC_DRAW -- every current use case (built-in primitives, imported meshes) is
        // upload-once-never-updated. Binds the VBO itself; only requires this VAO to already be
        // bound (see Bind()).
        void SetVertexData(const void* data, size_t sizeBytes, int vertexCount);

        // Describes one `componentCount`-float vertex attribute at `location` (as returned by
        // GLShaderProgram::GetAttribLocation), `stride`/`offset` in bytes -- mirrors a single
        // glVertexAttribPointer call. Must be called while this VAO is bound (Bind() first).
        void AddFloatAttribute(int location, int componentCount, size_t stride, size_t offset);

        int GetVertexCount() const { return m_VertexCount; }

        // Submesh ranges (see MeshData::SubMesh) for an imported mesh -- empty for the 3
        // built-in procedural primitives, which have no material-group concept and are always
        // drawn as one whole mesh. Stored directly on this object (not a separate parallel
        // container elsewhere) so it's cleared for free by every existing move/cache-eviction
        // site that already handles the rest of this object's lifetime -- a second, independently
        // maintained container keyed by the same handle would risk desyncing after a cache
        // eviction, silently corrupting an unrelated mesh's draw range.
        void SetSubMeshes(std::vector<MeshData::SubMesh> subMeshes) { m_SubMeshes = std::move(subMeshes); }
        const std::vector<MeshData::SubMesh>& GetSubMeshes() const { return m_SubMeshes; }

    private:
        unsigned int m_Vao = 0;
        unsigned int m_Vbo = 0;
        int m_VertexCount = 0;
        std::vector<MeshData::SubMesh> m_SubMeshes;
    };

}
