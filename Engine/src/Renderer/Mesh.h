#pragma once

#include "Core/Base.h"
#include "Renderer/VertexArray.h"

#include <cstdint>
#include <string>

namespace Engine
{

    class Mesh
    {
    public:
        ~Mesh() = default;

        const Ref<VertexArray>& GetVertexArray() const
        {
            return m_VertexArray;
        }

        uint32_t GetIndexCount() const
        {
            return m_IndexCount;
        }

        const std::string& GetMeshType() const
        {
            return m_MeshType;
        }

        static Ref<Mesh> CreateCube();
        static Ref<Mesh> CreatePlane();
        static Ref<Mesh> CreateSphere(int sectors = 36, int stacks = 18);

    private:
        struct PrivateTag
        {
        };

    public:
        Mesh(PrivateTag, const Ref<VertexArray>& vertexArray, uint32_t indexCount, const std::string& meshType)
            : m_VertexArray(vertexArray), m_IndexCount(indexCount), m_MeshType(meshType)
        {
        }

    private:
        Ref<VertexArray> m_VertexArray;
        uint32_t m_IndexCount = 0;
        std::string m_MeshType;
    };

} // namespace Engine
