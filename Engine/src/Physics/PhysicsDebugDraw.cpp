#include "engpch.h"
#include "Physics/PhysicsDebugDraw.h"
#include "Scene/Components.h"
#include "Terrain/TerrainMeshGenerator.h"
#include "Renderer/Shader.h"
#include "Renderer/EditorCamera.h"
#include "Renderer/RenderCommand.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cmath>

namespace Engine
{

    void PhysicsDebugDraw::Init()
    {
        if (m_Initialized)
            return;

        // 物理调试线段着色器
        m_LineShader = Shader::Create("assets/shaders/PhysicsDebugLine.glsl");

        // 创建动态 VBO（预分配空间，后续用 SetData 更新）
        m_LineVBO = VertexBuffer::Create(sizeof(LineVertex) * 65536);
        m_LineVBO->SetLayout({
            {ShaderDataType::Float3, "a_Position"},
            {ShaderDataType::Float3, "a_Color"},
        });

        m_LineVAO = VertexArray::Create();
        m_LineVAO->AddVertexBuffer(m_LineVBO);

        m_Initialized = true;
    }

    void PhysicsDebugDraw::DrawColliders(entt::registry& reg, const EditorCamera& camera)
    {
        if (!m_Initialized)
            Init();

        m_LineVertices.clear();

        // 绿色画碰撞体线框
        glm::vec3 boxColor = {0.0f, 1.0f, 0.0f};
        glm::vec3 sphereColor = {0.0f, 0.8f, 1.0f};

        // 盒碰撞器
        {
            auto view = reg.view<TransformComponent, BoxColliderComponent>();
            for (auto entity : view)
            {
                auto& transform = view.get<TransformComponent>(entity);
                auto& box = view.get<BoxColliderComponent>(entity);

                glm::vec3 center = transform.Translation + box.Offset;
                glm::vec3 halfExtents = box.HalfExtents * transform.Scale;
                DrawBox(center, halfExtents, transform.Rotation, boxColor);
            }
        }

        // 球碰撞器
        {
            auto view = reg.view<TransformComponent, SphereColliderComponent>();
            for (auto entity : view)
            {
                auto& transform = view.get<TransformComponent>(entity);
                auto& sphere = view.get<SphereColliderComponent>(entity);

                glm::vec3 center = transform.Translation + sphere.Offset;
                float maxScale = std::max({transform.Scale.x, transform.Scale.y, transform.Scale.z});
                float radius = sphere.Radius * maxScale;
                DrawSphere(center, radius, sphereColor);
            }
        }

        // 网格碰撞器（以紫色方框简易表示 AABB）
        {
            glm::vec3 meshColor = {0.8f, 0.2f, 0.8f}; // 紫色
            auto view = reg.view<TransformComponent, MeshColliderComponent>();
            for (auto entity : view)
            {
                auto& transform = view.get<TransformComponent>(entity);
                // 网格碰撞器用 AABB 线框作简易可视化（实际碰撞形状由 Bullet 管理）
                glm::vec3 halfExtents = transform.Scale * 0.5f;
                DrawBox(transform.Translation, halfExtents, transform.Rotation, meshColor);
            }
        }

        // 地形碰撞器
        {
            glm::vec3 terrainColor = {1.0f, 0.6f, 0.0f}; // 橙色
            auto view = reg.view<TransformComponent, TerrainComponent>();
            for (auto entity : view)
            {
                auto& transform = view.get<TransformComponent>(entity);
                DrawTerrainWireframe(transform.Translation, reg, entity);
            }
        }

        if (!m_LineVertices.empty())
            Flush(camera.GetViewProjection());
    }

    void PhysicsDebugDraw::DrawLine(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color)
    {
        m_LineVertices.push_back({from, color});
        m_LineVertices.push_back({to, color});
    }

    void PhysicsDebugDraw::DrawBox(const glm::vec3& center, const glm::vec3& half, const glm::vec3& rotation, const glm::vec3& color)
    {
        // 用四元数旋转 8 个局部顶点到世界空间
        glm::quat q(rotation);

        glm::vec3 localVerts[8] = {
            {-half.x, -half.y, -half.z},
            { half.x, -half.y, -half.z},
            { half.x,  half.y, -half.z},
            {-half.x,  half.y, -half.z},
            {-half.x, -half.y,  half.z},
            { half.x, -half.y,  half.z},
            { half.x,  half.y,  half.z},
            {-half.x,  half.y,  half.z},
        };

        glm::vec3 v[8];
        for (int i = 0; i < 8; i++)
            v[i] = center + q * localVerts[i];

        // 12 条边
        // 底面
        DrawLine(v[0], v[1], color); DrawLine(v[1], v[2], color);
        DrawLine(v[2], v[3], color); DrawLine(v[3], v[0], color);
        // 顶面
        DrawLine(v[4], v[5], color); DrawLine(v[5], v[6], color);
        DrawLine(v[6], v[7], color); DrawLine(v[7], v[4], color);
        // 竖边
        DrawLine(v[0], v[4], color); DrawLine(v[1], v[5], color);
        DrawLine(v[2], v[6], color); DrawLine(v[3], v[7], color);
    }

    void PhysicsDebugDraw::DrawSphere(const glm::vec3& center, float radius, const glm::vec3& color)
    {
        constexpr int segments = 24;
        constexpr float step = 2.0f * glm::pi<float>() / segments;

        // 3 个圆环（XY, XZ, YZ 平面）
        for (int i = 0; i < segments; i++)
        {
            float a0 = i * step;
            float a1 = (i + 1) * step;

            // XY 平面
            DrawLine(
                center + glm::vec3(std::cos(a0) * radius, std::sin(a0) * radius, 0),
                center + glm::vec3(std::cos(a1) * radius, std::sin(a1) * radius, 0),
                color);

            // XZ 平面
            DrawLine(
                center + glm::vec3(std::cos(a0) * radius, 0, std::sin(a0) * radius),
                center + glm::vec3(std::cos(a1) * radius, 0, std::sin(a1) * radius),
                color);

            // YZ 平面
            DrawLine(
                center + glm::vec3(0, std::cos(a0) * radius, std::sin(a0) * radius),
                center + glm::vec3(0, std::cos(a1) * radius, std::sin(a1) * radius),
                color);
        }
    }

    void PhysicsDebugDraw::DrawTerrainWireframe(const glm::vec3& translation, entt::registry& reg, entt::entity entity)
    {
        auto& tc = reg.get<TerrainComponent>(entity);
        auto* meshData = tc.RuntimeMeshData;
        if (!meshData || meshData->HeightData.empty())
            return;

        glm::vec3 color = {1.0f, 0.6f, 0.0f}; // 橙色

        int hmW = meshData->HeightmapWidth;
        int hmH = meshData->HeightmapHeight;
        float size = tc.TerrainSize;
        float heightScale = tc.HeightScale;
        float halfSize = size * 0.5f;

        // 简化线框: 每隔 step 个网格点画一条线（避免线太多）
        int step = std::max(1, std::max(hmW, hmH) / 32);

        auto getWorldPos = [&](int ix, int iy) -> glm::vec3
        {
            float u = static_cast<float>(ix) / static_cast<float>(hmW - 1);
            float v = static_cast<float>(iy) / static_cast<float>(hmH - 1);
            int idx = std::min(iy, hmH - 1) * hmW + std::min(ix, hmW - 1);
            float h = meshData->HeightData[idx];
            return translation + glm::vec3(
                u * size - halfSize,
                h * heightScale,
                v * size - halfSize
            );
        };

        // 画 X 方向线
        for (int j = 0; j < hmH; j += step)
        {
            for (int i = 0; i < hmW - 1; i += step)
            {
                int nextI = std::min(i + step, hmW - 1);
                DrawLine(getWorldPos(i, j), getWorldPos(nextI, j), color);
            }
        }

        // 画 Z 方向线
        for (int i = 0; i < hmW; i += step)
        {
            for (int j = 0; j < hmH - 1; j += step)
            {
                int nextJ = std::min(j + step, hmH - 1);
                DrawLine(getWorldPos(i, j), getWorldPos(i, nextJ), color);
            }
        }
    }

    void PhysicsDebugDraw::Flush(const glm::mat4& viewProjection)
    {
        // 关闭深度测试，让线框始终可见（不被物体遮挡）
        RenderCommand::SetDepthTest(false);

        m_LineShader->Bind();
        m_LineShader->SetMat4("u_ViewProjection", viewProjection);

        m_LineVBO->SetData(m_LineVertices.data(),
                           static_cast<uint32_t>(m_LineVertices.size() * sizeof(LineVertex)));

        m_LineVAO->Bind();
        RenderCommand::SetLineWidth(2.0f);
        RenderCommand::DrawLines(static_cast<uint32_t>(m_LineVertices.size()), 0);
        RenderCommand::SetLineWidth(1.0f);

        // 恢复深度测试
        RenderCommand::SetDepthTest(true);
    }

} // namespace Engine
