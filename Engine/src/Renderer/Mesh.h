#pragma once

#include "Asset/AssetHandle.h"
#include "Core/Base.h"
#include "Renderer/VertexArray.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Engine
{

    struct SubMesh
    {
        Ref<VertexArray> VAO;
        uint32_t IndexCount = 0;
        std::string DiffuseTexturePath;  // relative path (extracted from model)
        AssetHandle DiffuseTextureAsset; // loaded texture handle
        std::string NormalTexturePath;   // relative path (extracted from model)
        AssetHandle NormalTextureAsset;  // loaded normal map handle
    };

    class Mesh
    {
    public:
        ~Mesh() = default;

        const std::vector<SubMesh>& GetSubMeshes() const { return m_SubMeshes; }

        size_t GetSubMeshCount() const { return m_SubMeshes.size(); }

        // Backward compat: first submesh
        const Ref<VertexArray>& GetVertexArray() const { return m_SubMeshes[0].VAO; }

        uint32_t GetIndexCount() const { return m_SubMeshes[0].IndexCount; }

        const std::string& GetMeshType() const { return m_MeshType; }

        const std::string& GetModelPath() const { return m_ModelPath; }

        static Ref<Mesh> CreateCube();
        static Ref<Mesh> CreatePlane();
        static Ref<Mesh> CreateSphere(int sectors = 36, int stacks = 18);
        static Ref<Mesh> CreateFromFile(const std::string& filepath);

    private:
        struct PrivateTag
        {
        };

    public:
        // Single-submesh constructor (primitives)
        Mesh(PrivateTag, const Ref<VertexArray>& vertexArray, uint32_t indexCount, const std::string& meshType)
            : m_MeshType(meshType)
        {
            SubMesh sub;
            sub.VAO = vertexArray;
            sub.IndexCount = indexCount;
            m_SubMeshes.push_back(std::move(sub));
        }

        // Multi-submesh constructor (model files)
        Mesh(PrivateTag, std::vector<SubMesh>&& subMeshes, const std::string& meshType, const std::string& modelPath)
            : m_SubMeshes(std::move(subMeshes)), m_MeshType(meshType), m_ModelPath(modelPath)
        {
        }

    private:
        std::vector<SubMesh> m_SubMeshes;
        std::string m_MeshType;
        std::string m_ModelPath; // non-empty for loaded models
    };

} // namespace Engine
