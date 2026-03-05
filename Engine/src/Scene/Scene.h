#pragma once

#include "Core/Base.h"
#include "Core/Timestep.h"
#include "Core/UUID.h"
#include "Scene/Systems/ShadowSystem.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <string>
#include <vector>
#include <memory>

namespace Engine
{

    class Entity;
    class EditorCamera;
    class SceneRenderer;
    class PhysicsWorld;
    class BulletPhysicsWorld;

    enum class PhysicsBackend { Custom = 0, Bullet = 1 };

    class Scene
    {
    public:
        Scene();
        ~Scene();

        Entity CreateEntity(const std::string& name = "Entity");
        Entity CreateEntityWithUUID(UUID uuid, const std::string& name = "Entity");
        void DestroyEntity(Entity entity);

        // 父子层级接口
        void SetParent(Entity child, Entity parent);
        void RemoveParent(Entity child);
        std::vector<Entity> GetChildren(Entity parent);
        Entity FindEntityByUUID(UUID uuid);
        bool IsAncestorOf(Entity ancestor, Entity entity);
        glm::mat4 GetWorldTransform(Entity entity);
        std::vector<Entity> GetRootEntities();

        void OnUpdateEditor(Timestep ts, EditorCamera& camera);
        void OnUpdateRuntime(Timestep ts, EditorCamera& camera);
        void OnRuntimeStart();
        void OnRuntimeStop();

        static Ref<Scene> Copy(Ref<Scene> src);

        void OnViewportResize(uint32_t width, uint32_t height);

        // 渲染器绑定
        void SetSceneRenderer(SceneRenderer* renderer) { m_SceneRenderer = renderer; }
        SceneRenderer* GetSceneRenderer() const { return m_SceneRenderer; }

        // Skybox 委托
        void LoadSkybox(const std::vector<std::string>& facePaths);
        void ClearSkybox();
        bool HasSkybox() const;
        const std::vector<std::string>& GetSkyboxFacePaths() const;

        // Shadow 委托
        ShadowSettings& GetShadowSettings();
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

        PhysicsBackend GetPhysicsBackend() const { return m_PhysicsBackend; }
        void SetPhysicsBackend(PhysicsBackend backend) { m_PhysicsBackend = backend; }

    private:
        entt::registry m_Registry;
        uint32_t m_ViewportWidth = 0;
        uint32_t m_ViewportHeight = 0;

        // 渲染器引用（非所有权）
        SceneRenderer* m_SceneRenderer = nullptr;

        // Physics
        PhysicsBackend m_PhysicsBackend = PhysicsBackend::Custom;
        std::unique_ptr<PhysicsWorld> m_PhysicsWorld;
        std::unique_ptr<BulletPhysicsWorld> m_BulletPhysicsWorld;

        // 备份用的 ShadowSettings（SceneRenderer 为 null 时使用）
        ShadowSettings m_FallbackShadowSettings;

        friend class Entity;
    };

} // namespace Engine
