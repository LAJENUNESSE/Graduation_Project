#pragma once

#include "Core/Base.h"
#include "Core/Timestep.h"
#include "Core/UUID.h"
#include "Renderer/Texture.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <string>

namespace Engine
{

    class Entity;
    class EditorCamera;
    class Shader;
    class Framebuffer;

    struct ShadowSettings
    {
        bool Enabled = true;
        int MapResolution = 1024;
        float Bias = 0.005f;
        float OrthoSize = 20.0f;
        float NearPlane = 0.1f;
        float FarPlane = 50.0f;
    };

    class Scene
    {
    public:
        Scene();
        ~Scene();

        Entity CreateEntity(const std::string& name = "Entity");
        Entity CreateEntityWithUUID(UUID uuid, const std::string& name = "Entity");
        void DestroyEntity(Entity entity);

        void OnUpdateEditor(Timestep ts, EditorCamera& camera);

        void OnViewportResize(uint32_t width, uint32_t height);

        void ShadowPass();
        ShadowSettings& GetShadowSettings() { return m_ShadowSettings; }
        void ResizeShadowMap(int resolution);

        template <typename... Components>
        auto GetAllEntitiesWith()
        {
            return m_Registry.view<Components...>();
        }

        entt::registry& GetRegistry()
        {
            return m_Registry;
        }

    private:
        entt::registry m_Registry;
        uint32_t m_ViewportWidth = 0;
        uint32_t m_ViewportHeight = 0;
        Ref<Shader> m_MeshShader;
        Ref<Texture2D> m_WhiteTexture;

        // Shadow mapping
        Ref<Shader> m_DepthShader;
        Ref<Framebuffer> m_ShadowMapFBO;
        ShadowSettings m_ShadowSettings;
        glm::mat4 m_LightSpaceMatrix{1.0f};
        bool m_HasValidShadowCaster = false;

        friend class Entity;
    };

} // namespace Engine
