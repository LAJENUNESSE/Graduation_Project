#pragma once

#include "Core/Base.h"
#include "Renderer/Shader.h"

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <vector>

namespace Engine
{

    class EditorCamera;

    class PhysicsDebugDraw
    {
    public:
        void Init();
        void DrawColliders(entt::registry& reg, const EditorCamera& camera);

    private:
        void DrawLine(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color);
        void DrawBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& rotation, const glm::vec3& color);
        void DrawSphere(const glm::vec3& center, float radius, const glm::vec3& color);
        void Flush(const glm::mat4& viewProjection);

        struct LineVertex
        {
            glm::vec3 Position;
            glm::vec3 Color;
        };

        std::vector<LineVertex> m_LineVertices;
        Ref<Shader> m_LineShader;
        uint32_t m_LineVAO = 0;
        uint32_t m_LineVBO = 0;
        bool m_Initialized = false;
    };

} // namespace Engine
