#include "engpch.h"
#include "Scene/Scene.h"
#include "Core/Log.h"
#include "Reflection/ComponentRegistry.h"
#include "Renderer/EditorCamera.h"
#include "Renderer/SceneRenderer.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Runtime/RuntimeComponents.h"
#include "Scene/Runtime/SceneRuntimeCoordinator.h"
#include "Scene/WorldTransformService.h"
#include "Scene/SceneHierarchyService.h"
#include "Script/NativeScriptComponent.h"

namespace Engine
{

    Scene::Scene()
    {
        ComponentRegistry::EnsureRegistered();
        m_RuntimeCoordinator = std::make_unique<SceneRuntimeCoordinator>(
            m_Registry, m_EntityIndex, m_LifecycleCoordinator, this);
    }

    Scene::~Scene()
    {
        m_RuntimeCoordinator.reset();
        m_LifecycleCoordinator.CleanupSceneDestroy();
    }

    Entity Scene::CreateEntity(const std::string& name)
    {
        return CreateEntityWithUUID(UUID(), name);
    }

    Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
    {
        Entity entity = {m_Registry.create(), this};
        entity.AddComponent<IDComponent>(uuid);
        entity.AddComponent<TagComponent>(name);
        entity.AddComponent<TransformComponent>();
        entity.AddComponent<RelationshipComponent>();
        m_EntityIndex.Insert(uuid, entity);
        return entity;
    }

    void Scene::DestroyEntity(Entity entity)
    {
        // 递归销毁所有子实体
        if (entity.HasComponent<RelationshipComponent>())
        {
            auto children = entity.GetComponent<RelationshipComponent>().Children;
            for (auto childUUID : children)
            {
                Entity child = FindEntityByUUID(childUUID);
                if (child)
                    DestroyEntity(child);
            }

            // 从父实体的 Children 列表中移除自身
            auto& rel = entity.GetComponent<RelationshipComponent>();
            if (static_cast<uint64_t>(rel.ParentID) != 0)
            {
                Entity parent = FindEntityByUUID(rel.ParentID);
                if (parent && parent.HasComponent<RelationshipComponent>())
                {
                    auto& parentChildren = parent.GetComponent<RelationshipComponent>().Children;
                    UUID myUUID = entity.GetUUID();
                    parentChildren.erase(
                        std::remove_if(parentChildren.begin(), parentChildren.end(), [myUUID](UUID id)
                                       { return static_cast<uint64_t>(id) == static_cast<uint64_t>(myUUID); }),
                        parentChildren.end());
                }
            }
        }

        if (m_RuntimeCoordinator)
            m_RuntimeCoordinator->DestroyPhysicsBody((entt::entity)entity);

        m_LifecycleCoordinator.PreDestroyEntity(m_Registry, entity);

        UUID uuid = entity.GetUUID();
        m_Registry.destroy(entity);
        m_EntityIndex.Remove(uuid);
    }

    void Scene::SetParent(Entity child, Entity parent)
    {
        SceneHierarchyService::SetParent(m_Registry, m_EntityIndex, (entt::entity)child, (entt::entity)parent);
    }

    void Scene::RemoveParent(Entity child)
    {
        SceneHierarchyService::RemoveParent(m_Registry, m_EntityIndex, (entt::entity)child);
    }

    std::vector<Entity> Scene::GetChildren(Entity parent)
    {
        auto entities = SceneHierarchyService::GetChildren(m_Registry, m_EntityIndex, (entt::entity)parent);
        std::vector<Entity> result;
        for (auto e : entities)
            result.push_back(Entity{e, this});
        return result;
    }

    Entity Scene::FindEntityByUUID(UUID uuid)
    {
        entt::entity entity = m_EntityIndex.Find(uuid);
        if (entity != entt::null && m_Registry.valid(entity))
            return Entity{entity, this};
        return {};
    }

    bool Scene::IsAncestorOf(Entity ancestor, Entity entity)
    {
        return SceneHierarchyService::IsAncestorOf(m_Registry, m_EntityIndex,
                                                    (entt::entity)ancestor, (entt::entity)entity);
    }

    glm::mat4 Scene::GetWorldTransform(Entity entity)
    {
        if (!entity || !entity.HasComponent<TransformComponent>())
            return glm::mat4(1.0f);

        // 编辑器/工具侧查询可能与同帧的 Transform 写入交错，不能复用渲染缓存。
        return WorldTransformService::ComputeWorldTransform(m_Registry, (entt::entity)entity, m_EntityIndex);
    }

    std::vector<Entity> Scene::GetRootEntities()
    {
        auto entities = SceneHierarchyService::GetRootEntities(m_Registry);
        std::vector<Entity> result;
        for (auto e : entities)
            result.push_back(Entity{e, this});
        return result;
    }

    void Scene::OnUpdateEditor(Timestep ts, EditorCamera& camera)
    {
        m_TransformCache.BeginFrame();
    }

    void Scene::OnUpdateRuntime(Timestep ts, EditorCamera& camera)
    {
        m_TransformCache.BeginFrame();
        m_RuntimeCoordinator->OnUpdateRuntime(ts, m_PhysicsBackend, m_SceneRenderer);
    }

    void Scene::OnRuntimeStart()
    {
        m_RuntimeCoordinator->OnRuntimeStart(m_PhysicsBackend, m_SceneRenderer);
    }

    void Scene::OnRuntimeStop()
    {
        m_RuntimeCoordinator->OnRuntimeStop(m_SceneRenderer);
    }

    Ref<Scene> Scene::Copy(Ref<Scene> src)
    {
        auto newScene = CreateRef<Scene>();

        newScene->m_ViewportWidth = src->m_ViewportWidth;
        newScene->m_ViewportHeight = src->m_ViewportHeight;
        newScene->m_PhysicsBackend = src->m_PhysicsBackend;

        // 拷贝环境数据（Shadow + Skybox）— 直接拷贝 scene 状态，不从 renderer 反向拉取
        newScene->m_EnvironmentState = src->m_EnvironmentState;

        // Copy all entities
        auto& srcReg = src->m_Registry;
        auto view = srcReg.view<IDComponent>();
        for (auto srcEntity : view)
        {
            UUID uuid = srcReg.get<IDComponent>(srcEntity).ID;
            const auto& name = srcReg.get<TagComponent>(srcEntity).Tag;
            Entity newEntity = newScene->CreateEntityWithUUID(uuid, name);

            if (srcReg.all_of<TransformComponent>(srcEntity))
                newEntity.GetComponent<TransformComponent>() = srcReg.get<TransformComponent>(srcEntity);

            // RelationshipComponent: CreateEntityWithUUID 已添加默认的，直接覆盖数据
            if (srcReg.all_of<RelationshipComponent>(srcEntity))
                newEntity.GetComponent<RelationshipComponent>() = srcReg.get<RelationshipComponent>(srcEntity);

            if (srcReg.all_of<MeshRendererComponent>(srcEntity))
            {
                auto mrc = srcReg.get<MeshRendererComponent>(srcEntity);
                mrc.CachedMaterials.clear(); // 避免拷贝场景与原场景共享 Material
                newEntity.AddComponent<MeshRendererComponent>(mrc);
            }

            if (srcReg.all_of<CameraComponent>(srcEntity))
                newEntity.AddComponent<CameraComponent>(srcReg.get<CameraComponent>(srcEntity));

            // 反射注册的组件通过 ComponentRegistry 自动拷贝（跳过需要特殊处理的 CustomSerial 组件）
            uint32_t srcId = static_cast<uint32_t>(srcEntity);
            uint32_t dstId = static_cast<uint32_t>(static_cast<entt::entity>(newEntity));
            for (auto& meta : ComponentRegistry::Instance().GetAll())
            {
                if (meta.Flags & ComponentMeta::CustomSerial)
                    continue;

                if (meta.Has(*src, srcId))
                    meta.Copy(*src, srcId, *newScene, dstId);
            }

            if (srcReg.all_of<TerrainComponent>(srcEntity))
            {
                auto tc = srcReg.get<TerrainComponent>(srcEntity);
                tc.MeshDirty = true; // 新场景重建网格（runtime 组件不拷贝）
                newEntity.AddComponent<TerrainComponent>(tc);
            }

            if (srcReg.all_of<ParticleEmitterComponent>(srcEntity))
            {
                auto pe = srcReg.get<ParticleEmitterComponent>(srcEntity);
                pe.CollisionBurstCount = 0;
                newEntity.AddComponent<ParticleEmitterComponent>(pe);
            }

            // NativeScriptComponent: 只拷贝 ScriptName，不拷运行时实例
            if (srcReg.all_of<NativeScriptComponent>(srcEntity))
            {
                auto& srcNsc = srcReg.get<NativeScriptComponent>(srcEntity);
                auto& dstNsc = newEntity.AddComponent<NativeScriptComponent>();
                dstNsc.ScriptName = srcNsc.ScriptName;
                dstNsc.InstantiateScript = srcNsc.InstantiateScript;
                dstNsc.DestroyScript = srcNsc.DestroyScript;
                // Instance 不拷贝（运行时创建）
            }
        }

        size_t srcEntityCount = 0;
        for (auto entity : srcReg.view<IDComponent>())
        {
            (void)entity;
            ++srcEntityCount;
        }

        size_t dstEntityCount = 0;
        for (auto entity : newScene->m_Registry.view<IDComponent>())
        {
            (void)entity;
            ++dstEntityCount;
        }

        ENGINE_CORE_INFO("[SceneLifecycle] Scene::Copy completed srcEntities={0}, dstEntities={1}", srcEntityCount,
                         dstEntityCount);

        return newScene;
    }

    void Scene::OnViewportResize(uint32_t width, uint32_t height)
    {
        m_ViewportWidth = width;
        m_ViewportHeight = height;

        auto view = m_Registry.view<CameraComponent>();
        for (auto entity : view)
        {
            auto& cameraComponent = view.get<CameraComponent>(entity);
            if (!cameraComponent.FixedAspectRatio)
                cameraComponent.Camera.SetViewportSize(width, height);
        }
    }

    void Scene::SetSceneRenderer(SceneRenderer* renderer)
    {
        m_SceneRenderer = renderer;
        if (!renderer)
            return;

        auto& shadowSystem = renderer->GetShadowSystem();
        shadowSystem.GetSettings() = m_EnvironmentState.Shadow;
        shadowSystem.ResizeShadowMap(m_EnvironmentState.Shadow.MapResolution);

        if (m_EnvironmentState.SkyboxFacePaths.empty())
            renderer->GetSkyboxSystem().ClearSkybox();
        else
            renderer->GetSkyboxSystem().LoadSkybox(m_EnvironmentState.SkyboxFacePaths);

        // 注册编辑态粒子/流体清理回调（运行时 OnRuntimeStart 会 ClearAll 后重新注册完整版本）
        m_LifecycleCoordinator.RegisterEntityCleanup([renderer](entt::registry& reg, entt::entity e)
        {
            uint32_t eid = static_cast<uint32_t>(e);
            if (reg.all_of<ParticleEmitterComponent>(e))
                renderer->ReleaseParticleSystem(eid);
            if (reg.all_of<FluidEmitterComponent>(e))
                renderer->ReleaseFluidSystem(eid);
        });
    }

    // Skybox 委托
    void Scene::LoadSkybox(const std::vector<std::string>& facePaths)
    {
        m_EnvironmentState.SkyboxFacePaths = facePaths;
        if (m_SceneRenderer)
            m_SceneRenderer->GetSkyboxSystem().LoadSkybox(facePaths);
    }

    void Scene::ClearSkybox()
    {
        m_EnvironmentState.SkyboxFacePaths.clear();
        if (m_SceneRenderer)
            m_SceneRenderer->GetSkyboxSystem().ClearSkybox();
    }

    bool Scene::HasSkybox() const
    {
        return !m_EnvironmentState.SkyboxFacePaths.empty();
    }

    const std::vector<std::string>& Scene::GetSkyboxFacePaths() const
    {
        return m_EnvironmentState.SkyboxFacePaths;
    }

    // Shadow 委托
    ShadowSettings& Scene::GetShadowSettings()
    {
        return m_EnvironmentState.Shadow;
    }

    void Scene::ResizeShadowMap(int resolution)
    {
        m_EnvironmentState.Shadow.MapResolution = resolution;
        if (m_SceneRenderer)
        {
            m_SceneRenderer->GetShadowSystem().GetSettings().MapResolution = resolution;
            m_SceneRenderer->GetShadowSystem().ResizeShadowMap(resolution);
        }
    }

    void Scene::SyncEnvironmentFromRenderer()
    {
        if (!m_SceneRenderer)
            return;

        m_EnvironmentState.Shadow = m_SceneRenderer->GetShadowSystem().GetSettings();
        m_EnvironmentState.SkyboxFacePaths = m_SceneRenderer->GetSkyboxSystem().GetFacePaths();
    }

} // namespace Engine



