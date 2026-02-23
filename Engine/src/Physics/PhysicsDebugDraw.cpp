#include "engpch.h"
#include "Physics/PhysicsDebugDraw.h"
#include "Scene/Components.h"
#include "Renderer/Shader.h"
#include "Renderer/EditorCamera.h"

#include <glad/gl.h>
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

        // 简单的线段着色器
        std::string vertSrc = R"(
            #version 330 core
            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec3 a_Color;

            uniform mat4 u_ViewProjection;

            out vec3 v_Color;

            void main() {
                v_Color = a_Color;
                gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
            }
        )";

        std::string fragSrc = R"(
            #version 330 core
            layout(location = 0) out vec4 o_Color;
            layout(location = 1) out int o_EntityID;

            in vec3 v_Color;

            void main() {
                o_Color = vec4(v_Color, 1.0);
                o_EntityID = -1;
            }
        )";

        m_LineShader = Shader::Create("PhysicsDebugLine", vertSrc, fragSrc);

        // 创建 VAO/VBO（动态更新）
        glGenVertexArrays(1, &m_LineVAO);
        glGenBuffers(1, &m_LineVBO);

        glBindVertexArray(m_LineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_LineVBO);

        // Position (location 0)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), (void*)0);
        glEnableVertexAttribArray(0);

        // Color (location 1)
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);

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

    void PhysicsDebugDraw::Flush(const glm::mat4& viewProjection)
    {
        // 关闭深度测试，让线框始终可见（不被物体遮挡）
        glDisable(GL_DEPTH_TEST);

        m_LineShader->Bind();
        m_LineShader->SetMat4("u_ViewProjection", viewProjection);

        glBindVertexArray(m_LineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_LineVBO);
        glBufferData(GL_ARRAY_BUFFER,
                     m_LineVertices.size() * sizeof(LineVertex),
                     m_LineVertices.data(),
                     GL_DYNAMIC_DRAW);

        glLineWidth(2.0f);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(m_LineVertices.size()));
        glLineWidth(1.0f);

        glBindVertexArray(0);

        // 恢复深度测试
        glEnable(GL_DEPTH_TEST);
    }

} // namespace Engine
