#include "engpch.h"
#include "Renderer/Mesh.h"
#include "Asset/AssetManager.h"

#include "Renderer/Buffer.h"
#include "Core/Log.h"
#include "Asset/PathUtils.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <cmath>
#include <filesystem>

namespace Engine
{

    static BufferLayout GetMeshVertexLayout()
    {
        return {
            {ShaderDataType::Float3, "a_Position"},
            {ShaderDataType::Float3, "a_Normal"},
            {ShaderDataType::Float2, "a_TexCoords"},
            {ShaderDataType::Float3, "a_Tangent"},
        };
    }

    Ref<Mesh> Mesh::CreateCube()
    {
        // 24 vertices: 4 per face, each face has its own normal and tangent
        // Layout: Position(3) + Normal(3) + TexCoords(2) + Tangent(3) = 11 floats/vertex
        // clang-format off
        float vertices[] = {
            // Front face (z = +0.5, normal = 0,0,1, tangent = 1,0,0)
            -0.5f, -0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   0.0f, 0.0f,   1.0f, 0.0f, 0.0f,
             0.5f, -0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   1.0f, 0.0f,   1.0f, 0.0f, 0.0f,
             0.5f,  0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   1.0f, 1.0f,   1.0f, 0.0f, 0.0f,
            -0.5f,  0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   0.0f, 1.0f,   1.0f, 0.0f, 0.0f,

            // Back face (z = -0.5, normal = 0,0,-1, tangent = -1,0,0)
             0.5f, -0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   0.0f, 0.0f,  -1.0f, 0.0f, 0.0f,
            -0.5f, -0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   1.0f, 0.0f,  -1.0f, 0.0f, 0.0f,
            -0.5f,  0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   1.0f, 1.0f,  -1.0f, 0.0f, 0.0f,
             0.5f,  0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   0.0f, 1.0f,  -1.0f, 0.0f, 0.0f,

            // Top face (y = +0.5, normal = 0,1,0, tangent = 1,0,0)
            -0.5f,  0.5f,  0.5f,   0.0f,  1.0f,  0.0f,   0.0f, 0.0f,   1.0f, 0.0f, 0.0f,
             0.5f,  0.5f,  0.5f,   0.0f,  1.0f,  0.0f,   1.0f, 0.0f,   1.0f, 0.0f, 0.0f,
             0.5f,  0.5f, -0.5f,   0.0f,  1.0f,  0.0f,   1.0f, 1.0f,   1.0f, 0.0f, 0.0f,
            -0.5f,  0.5f, -0.5f,   0.0f,  1.0f,  0.0f,   0.0f, 1.0f,   1.0f, 0.0f, 0.0f,

            // Bottom face (y = -0.5, normal = 0,-1,0, tangent = 1,0,0)
            -0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,   0.0f, 0.0f,   1.0f, 0.0f, 0.0f,
             0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,   1.0f, 0.0f,   1.0f, 0.0f, 0.0f,
             0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,   1.0f, 1.0f,   1.0f, 0.0f, 0.0f,
            -0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,   0.0f, 1.0f,   1.0f, 0.0f, 0.0f,

            // Right face (x = +0.5, normal = 1,0,0, tangent = 0,0,-1)
             0.5f, -0.5f,  0.5f,   1.0f,  0.0f,  0.0f,   0.0f, 0.0f,   0.0f, 0.0f, -1.0f,
             0.5f, -0.5f, -0.5f,   1.0f,  0.0f,  0.0f,   1.0f, 0.0f,   0.0f, 0.0f, -1.0f,
             0.5f,  0.5f, -0.5f,   1.0f,  0.0f,  0.0f,   1.0f, 1.0f,   0.0f, 0.0f, -1.0f,
             0.5f,  0.5f,  0.5f,   1.0f,  0.0f,  0.0f,   0.0f, 1.0f,   0.0f, 0.0f, -1.0f,

            // Left face (x = -0.5, normal = -1,0,0, tangent = 0,0,1)
            -0.5f, -0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,   0.0f, 0.0f,   0.0f, 0.0f, 1.0f,
            -0.5f, -0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,   1.0f, 0.0f,   0.0f, 0.0f, 1.0f,
            -0.5f,  0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 1.0f,
            -0.5f,  0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,   0.0f, 1.0f,   0.0f, 0.0f, 1.0f,
        };

        uint32_t indices[] = {
            // Front
             0,  1,  2,   2,  3,  0,
            // Back
             4,  5,  6,   6,  7,  4,
            // Top
             8,  9, 10,  10, 11,  8,
            // Bottom
            12, 13, 14,  14, 15, 12,
            // Right
            16, 17, 18,  18, 19, 16,
            // Left
            20, 21, 22,  22, 23, 20,
        };
        // clang-format on

        auto vertexArray = VertexArray::Create();

        auto vertexBuffer = VertexBuffer::Create(vertices, sizeof(vertices));
        vertexBuffer->SetLayout(GetMeshVertexLayout());
        vertexArray->AddVertexBuffer(vertexBuffer);

        auto indexBuffer = IndexBuffer::Create(indices, 36);
        vertexArray->SetIndexBuffer(indexBuffer);

        return CreateRef<Mesh>(PrivateTag{}, vertexArray, 36, "Cube");
    }

    Ref<Mesh> Mesh::CreatePlane()
    {
        // 4 vertices in XZ plane at y=0, normal pointing up, tangent along +X
        // Layout: Position(3) + Normal(3) + TexCoords(2) + Tangent(3) = 11 floats/vertex
        // clang-format off
        float vertices[] = {
            // Position              Normal            TexCoords    Tangent
            -0.5f, 0.0f,  0.5f,   0.0f, 1.0f, 0.0f,   0.0f, 0.0f,   1.0f, 0.0f, 0.0f,
             0.5f, 0.0f,  0.5f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f,   1.0f, 0.0f, 0.0f,
             0.5f, 0.0f, -0.5f,   0.0f, 1.0f, 0.0f,   1.0f, 1.0f,   1.0f, 0.0f, 0.0f,
            -0.5f, 0.0f, -0.5f,   0.0f, 1.0f, 0.0f,   0.0f, 1.0f,   1.0f, 0.0f, 0.0f,
        };

        uint32_t indices[] = {
            0, 1, 2,
            2, 3, 0,
        };
        // clang-format on

        auto vertexArray = VertexArray::Create();

        auto vertexBuffer = VertexBuffer::Create(vertices, sizeof(vertices));
        vertexBuffer->SetLayout(GetMeshVertexLayout());
        vertexArray->AddVertexBuffer(vertexBuffer);

        auto indexBuffer = IndexBuffer::Create(indices, 6);
        vertexArray->SetIndexBuffer(indexBuffer);

        return CreateRef<Mesh>(PrivateTag{}, vertexArray, 6, "Plane");
    }

    Ref<Mesh> Mesh::CreateSphere(int sectors, int stacks)
    {
        if (sectors < 3)
            sectors = 3;
        if (stacks < 2)
            stacks = 2;

        std::vector<float> vertices;
        std::vector<uint32_t> indices;

        float pi = glm::pi<float>();
        float twoPi = 2.0f * pi;

        float sectorStep = twoPi / static_cast<float>(sectors);
        float stackStep = pi / static_cast<float>(stacks);

        // Generate vertices
        for (int i = 0; i <= stacks; ++i)
        {
            float stackAngle = pi / 2.0f - static_cast<float>(i) * stackStep; // from pi/2 to -pi/2
            float xy = std::cos(stackAngle);
            float z = std::sin(stackAngle);

            for (int j = 0; j <= sectors; ++j)
            {
                float sectorAngle = static_cast<float>(j) * sectorStep; // from 0 to 2pi

                // Position (unit sphere, radius = 0.5)
                float x = xy * std::cos(sectorAngle);
                float y = z;
                float xz = xy * std::sin(sectorAngle);

                // For a unit sphere, normal = position normalized (already unit length)
                float nx = x;
                float ny = y;
                float nz = xz;

                // Scale position to radius 0.5
                float px = x * 0.5f;
                float py = y * 0.5f;
                float pz = xz * 0.5f;

                // TexCoords
                float s = static_cast<float>(j) / static_cast<float>(sectors);
                float t = static_cast<float>(i) / static_cast<float>(stacks);

                // Tangent: derivative of position w.r.t. sectorAngle (normalized)
                float tx = -std::sin(sectorAngle);
                float ty = 0.0f;
                float tz = std::cos(sectorAngle);

                vertices.push_back(px);
                vertices.push_back(py);
                vertices.push_back(pz);
                vertices.push_back(nx);
                vertices.push_back(ny);
                vertices.push_back(nz);
                vertices.push_back(s);
                vertices.push_back(t);
                vertices.push_back(tx);
                vertices.push_back(ty);
                vertices.push_back(tz);
            }
        }

        // Generate indices
        for (int i = 0; i < stacks; ++i)
        {
            int k1 = i * (sectors + 1);
            int k2 = k1 + sectors + 1;

            for (int j = 0; j < sectors; ++j, ++k1, ++k2)
            {
                // Two triangles per sector (except at poles)
                if (i != 0)
                {
                    indices.push_back(static_cast<uint32_t>(k1));
                    indices.push_back(static_cast<uint32_t>(k2));
                    indices.push_back(static_cast<uint32_t>(k1 + 1));
                }

                if (i != (stacks - 1))
                {
                    indices.push_back(static_cast<uint32_t>(k1 + 1));
                    indices.push_back(static_cast<uint32_t>(k2));
                    indices.push_back(static_cast<uint32_t>(k2 + 1));
                }
            }
        }

        auto vertexArray = VertexArray::Create();

        auto vertexBuffer = VertexBuffer::Create(vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(float)));
        vertexBuffer->SetLayout(GetMeshVertexLayout());
        vertexArray->AddVertexBuffer(vertexBuffer);

        auto indexBuffer =
            IndexBuffer::Create(indices.data(), static_cast<uint32_t>(indices.size()));
        vertexArray->SetIndexBuffer(indexBuffer);

        return CreateRef<Mesh>(PrivateTag{}, vertexArray, static_cast<uint32_t>(indices.size()), "Sphere");
    }

    Ref<Mesh> Mesh::CreateFromFile(const std::string& filepath)
    {
        Assimp::Importer importer;
        const std::string resolvedFilepath = PathUtils::ResolvePathString(filepath);

        unsigned int flags = aiProcess_Triangulate
                           | aiProcess_GenSmoothNormals
                           | aiProcess_FlipUVs
                           | aiProcess_JoinIdenticalVertices
                           | aiProcess_CalcTangentSpace;

        const aiScene* scene = importer.ReadFile(resolvedFilepath, flags);

        if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
        {
            ENGINE_CORE_ERROR("Assimp failed to load '{0}': {1}", resolvedFilepath, importer.GetErrorString());
            return nullptr;
        }

        std::filesystem::path modelDir = std::filesystem::path(resolvedFilepath).parent_path();

        std::vector<SubMesh> subMeshes;

        for (unsigned int m = 0; m < scene->mNumMeshes; m++)
        {
            aiMesh* aiM = scene->mMeshes[m];

            std::vector<float> vertices;
            std::vector<uint32_t> indices;

            vertices.reserve(aiM->mNumVertices * 11);

            for (unsigned int v = 0; v < aiM->mNumVertices; v++)
            {
                // Position
                vertices.push_back(aiM->mVertices[v].x);
                vertices.push_back(aiM->mVertices[v].y);
                vertices.push_back(aiM->mVertices[v].z);

                // Normal
                if (aiM->HasNormals())
                {
                    vertices.push_back(aiM->mNormals[v].x);
                    vertices.push_back(aiM->mNormals[v].y);
                    vertices.push_back(aiM->mNormals[v].z);
                }
                else
                {
                    vertices.push_back(0.0f);
                    vertices.push_back(1.0f);
                    vertices.push_back(0.0f);
                }

                // TexCoords (first UV channel)
                if (aiM->mTextureCoords[0])
                {
                    vertices.push_back(aiM->mTextureCoords[0][v].x);
                    vertices.push_back(aiM->mTextureCoords[0][v].y);
                }
                else
                {
                    vertices.push_back(0.0f);
                    vertices.push_back(0.0f);
                }

                // Tangent
                if (aiM->HasTangentsAndBitangents())
                {
                    vertices.push_back(aiM->mTangents[v].x);
                    vertices.push_back(aiM->mTangents[v].y);
                    vertices.push_back(aiM->mTangents[v].z);
                }
                else
                {
                    vertices.push_back(1.0f);
                    vertices.push_back(0.0f);
                    vertices.push_back(0.0f);
                }
            }

            // Indices
            for (unsigned int f = 0; f < aiM->mNumFaces; f++)
            {
                aiFace& face = aiM->mFaces[f];
                for (unsigned int i = 0; i < face.mNumIndices; i++)
                    indices.push_back(face.mIndices[i]);
            }

            if (indices.empty())
                continue;

            // Create GPU buffers
            auto vertexArray = VertexArray::Create();

            auto vertexBuffer = VertexBuffer::Create(
                vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(float)));
            vertexBuffer->SetLayout(GetMeshVertexLayout());
            vertexArray->AddVertexBuffer(vertexBuffer);

            auto indexBuffer = IndexBuffer::Create(
                indices.data(), static_cast<uint32_t>(indices.size()));
            vertexArray->SetIndexBuffer(indexBuffer);

            SubMesh sub;
            sub.VAO = vertexArray;
            sub.IndexCount = static_cast<uint32_t>(indices.size());

            // Extract diffuse texture from material
            if (aiM->mMaterialIndex < scene->mNumMaterials)
            {
                aiMaterial* mat = scene->mMaterials[aiM->mMaterialIndex];
                if (mat->GetTextureCount(aiTextureType_DIFFUSE) > 0)
                {
                    aiString texPath;
                    mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath);

                    std::string texStr = texPath.C_Str();
                    if (!texStr.empty())
                    {
                        // Resolve relative to model directory
                        std::filesystem::path fullTexPath = modelDir / texStr;

                        if (std::filesystem::exists(fullTexPath))
                        {                            sub.DiffuseTexturePath = PathUtils::ToProjectRelativeOrAbsolute(fullTexPath);
                            sub.DiffuseTextureAsset = AssetManager::Load<Texture2D>(sub.DiffuseTexturePath);
                        }
                    }
                }

                // Extract normal map texture from material
                if (mat->GetTextureCount(aiTextureType_NORMALS) > 0 || mat->GetTextureCount(aiTextureType_HEIGHT) > 0)
                {
                    aiString texPath;
                    aiTextureType normalType = (mat->GetTextureCount(aiTextureType_NORMALS) > 0)
                        ? aiTextureType_NORMALS : aiTextureType_HEIGHT;
                    mat->GetTexture(normalType, 0, &texPath);

                    std::string texStr = texPath.C_Str();
                    if (!texStr.empty())
                    {
                        std::filesystem::path fullTexPath = modelDir / texStr;
                        if (std::filesystem::exists(fullTexPath))
                        {                            sub.NormalTexturePath = PathUtils::ToProjectRelativeOrAbsolute(fullTexPath);
                            sub.NormalTextureAsset = AssetManager::Load<Texture2D>(sub.NormalTexturePath);
                        }
                    }
                }
            }

            subMeshes.push_back(std::move(sub));
        }

        if (subMeshes.empty())
        {
            ENGINE_CORE_ERROR("No valid meshes found in '{0}'", filepath);
            return nullptr;
        }

        ENGINE_CORE_INFO("Loaded model '{0}': {1} submesh(es)", filepath, subMeshes.size());
        return CreateRef<Mesh>(PrivateTag{}, std::move(subMeshes), "Model", filepath);
    }

} // namespace Engine
