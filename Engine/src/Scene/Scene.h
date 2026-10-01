#pragma once

#include "Core/Base.h"
#include "Core/Timestep.h"
#include "Core/UUID.h"
#include "Scene/SceneEntityIndex.h"
#include "Scene/SceneEnvironmentState.h"
#include "Scene/WorldTransformCache.h"
#include "Scene/Runtime/ResourceLifecycleCoordinator.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Engine
{

    class Entity;
    class EditorCamera;
    class SceneRenderer;
    class SceneRuntimeCoordinator;

    class Scene
    {
    public:
        Scene();
        ~Scene();

        Entity CreateEntity(const std::string& name = "Entity");
        Entity CreateEntityWithUUID(UUID uuid, const std::string& name = "Entity");
        void   DestroyEntity(Entity entity);

        // 父子层级接口
        void                SetParent(Entity child, Entity parent);
        void                RemoveParent(Entity child);
        std::vector<Entity> GetChildren(Entity parent);
        Entity              FindEntityByUUID(UUID uuid);
        bool                IsAncestorOf(Entity ancestor, Entity entity);
        glm::mat4           GetWorldTransform(Entity entity);
        std::vector<Entity> GetRootEntities();

        void OnUpdateEditor(Timestep ts, EditorCamera& camera);
        void OnUpdateRuntime(Timestep ts, EditorCamera& camera);
        void OnRuntimeStart();
        void OnRuntimeStop();

        // 服务器权威：对运行中实体的 Dynamic 刚体设置线速度（无物理世界/非 Dynamic 返回 false）
        bool ApplyEntityLinearVelocity(entt::entity entity, const glm::vec3& velocity);

        // 读取运行中实体的当前线速度（无物理世界/体不存在返回 false）。
        // 注入水平输入前先读回，可保留 Bullet 累加的重力分量。
        bool ReadEntityLinearVelocity(entt::entity entity, glm::vec3& out) const;

        static Ref<Scene> Copy(Ref<Scene> src);

        void OnViewportResize(uint32_t width, uint32_t height);

        // 渲染器绑定
        void           SetSceneRenderer(SceneRenderer* renderer);
        SceneRenderer* GetSceneRenderer() const { return m_SceneRenderer; }

        // Skybox 委托
        void                            LoadSkybox(const std::vector<std::string>& facePaths);
        void                            ClearSkybox();
        bool                            HasSkybox() const;
        const std::vector<std::string>& GetSkyboxFacePaths() const;

        // Shadow 委托
        ShadowSettings& GetShadowSettings();
        void            ResizeShadowMap(int resolution);

        template <typename... Components> auto GetAllEntitiesWith() { return m_Registry.view<Components...>(); }

        entt::registry&               GetRegistry() { return m_Registry; }
        const SceneEntityIndex&       GetEntityIndex() const { return m_EntityIndex; }
        ResourceLifecycleCoordinator& GetLifecycleCoordinator() { return m_LifecycleCoordinator; }
        SceneEnvironmentState&        GetEnvironmentState() { return m_EnvironmentState; }
        WorldTransformCache&          GetTransformCache() { return m_TransformCache; }

    private:
        void SyncEnvironmentFromRenderer();

        entt::registry               m_Registry;
        SceneEntityIndex             m_EntityIndex;
        WorldTransformCache          m_TransformCache;
        ResourceLifecycleCoordinator m_LifecycleCoordinator;
        uint32_t                     m_ViewportWidth  = 0;
        uint32_t                     m_ViewportHeight = 0;

        // 渲染器引用（非所有权）
        SceneRenderer* m_SceneRenderer = nullptr;

        // 环境数据（Shadow + Skybox）：Scene 拥有，渲染器消费
        SceneEnvironmentState                    m_EnvironmentState;
        std::unique_ptr<SceneRuntimeCoordinator> m_RuntimeCoordinator;

        friend class Entity;
    };

} // namespace Engine
