#include "engpch.h"
#include "Renderer/Mesh.h"

#include "Renderer/Buffer.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>

namespace Engine
{

    static BufferLayout GetMeshVertexLayout()
    {
        return {
            {ShaderDataType::Float3, "a_Position"},
            {ShaderDataType::Float3, "a_Normal"},
            {ShaderDataType::Float2, "a_TexCoords"},
        };
    }

    Ref<Mesh> Mesh::CreateCube()
    {
        // 24 vertices: 4 per face, each face has its own normal
        // clang-format off
        float vertices[] = {
            // Front face (z = +0.5, normal = 0,0,1)
            -0.5f, -0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   0.0f, 0.0f,
             0.5f, -0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   1.0f, 0.0f,
             0.5f,  0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   1.0f, 1.0f,
            -0.5f,  0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   0.0f, 1.0f,

            // Back face (z = -0.5, normal = 0,0,-1)
             0.5f, -0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   0.0f, 0.0f,
            -0.5f, -0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   1.0f, 0.0f,
            -0.5f,  0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   1.0f, 1.0f,
             0.5f,  0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   0.0f, 1.0f,

            // Top face (y = +0.5, normal = 0,1,0)
            -0.5f,  0.5f,  0.5f,   0.0f,  1.0f,  0.0f,   0.0f, 0.0f,
             0.5f,  0.5f,  0.5f,   0.0f,  1.0f,  0.0f,   1.0f, 0.0f,
             0.5f,  0.5f, -0.5f,   0.0f,  1.0f,  0.0f,   1.0f, 1.0f,
            -0.5f,  0.5f, -0.5f,   0.0f,  1.0f,  0.0f,   0.0f, 1.0f,

            // Bottom face (y = -0.5, normal = 0,-1,0)
            -0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,   0.0f, 0.0f,
             0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,   1.0f, 0.0f,
             0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,   1.0f, 1.0f,
            -0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,   0.0f, 1.0f,

            // Right face (x = +0.5, normal = 1,0,0)
             0.5f, -0.5f,  0.5f,   1.0f,  0.0f,  0.0f,   0.0f, 0.0f,
             0.5f, -0.5f, -0.5f,   1.0f,  0.0f,  0.0f,   1.0f, 0.0f,
             0.5f,  0.5f, -0.5f,   1.0f,  0.0f,  0.0f,   1.0f, 1.0f,
             0.5f,  0.5f,  0.5f,   1.0f,  0.0f,  0.0f,   0.0f, 1.0f,

            // Left face (x = -0.5, normal = -1,0,0)
            -0.5f, -0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,   0.0f, 0.0f,
            -0.5f, -0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,   1.0f, 0.0f,
            -0.5f,  0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,   1.0f, 1.0f,
            -0.5f,  0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,   0.0f, 1.0f,
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
        // 4 vertices in XZ plane at y=0, normal pointing up
        // clang-format off
        float vertices[] = {
            // Position              Normal            TexCoords
            -0.5f, 0.0f,  0.5f,   0.0f, 1.0f, 0.0f,   0.0f, 0.0f,
             0.5f, 0.0f,  0.5f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f,
             0.5f, 0.0f, -0.5f,   0.0f, 1.0f, 0.0f,   1.0f, 1.0f,
            -0.5f, 0.0f, -0.5f,   0.0f, 1.0f, 0.0f,   0.0f, 1.0f,
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

                vertices.push_back(px);
                vertices.push_back(py);
                vertices.push_back(pz);
                vertices.push_back(nx);
                vertices.push_back(ny);
                vertices.push_back(nz);
                vertices.push_back(s);
                vertices.push_back(t);
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

} // namespace Engine
