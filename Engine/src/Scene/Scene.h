#pragma once

#include "Core/Base.h"
#include "Core/Timestep.h"
#include "Core/UUID.h"
#include "Renderer/Texture.h"

#include <entt/entt.hpp>

#include <string>

namespace Engine
{

    class Entity;
    class EditorCamera;
    class Shader;

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

        friend class Entity;
    };

} // namespace Engine
