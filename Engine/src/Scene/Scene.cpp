#include "engpch.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/EditorCamera.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/BulletPhysicsWorld.h"

namespace Engine
{

    Scene::Scene()
    {
    }

    Scene::~Scene()
    {
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
        return entity;
    }

    void Scene::DestroyEntity(Entity entity)
    {
        if (m_BulletPhysicsWorld)
            m_BulletPhysicsWorld->DestroyBody((entt::entity)entity);

        m_Registry.destroy(entity);
    }

    void Scene::OnUpdateEditor(Timestep ts, EditorCamera& camera)
    {
        // 编辑器模式：渲染由 EditorLayer 通过 SceneRenderer 驱动
    }

    void Scene::OnUpdateRuntime(Timestep ts, EditorCamera& camera)
    {
        // Physics step
        if (m_PhysicsBackend == PhysicsBackend::Custom && m_PhysicsWorld)
            m_PhysicsWorld->Step(ts, m_Registry);
        else if (m_PhysicsBackend == PhysicsBackend::Bullet && m_BulletPhysicsWorld)
            m_BulletPhysicsWorld->Step(ts, m_Registry);

        // 运行时渲染由 EditorLayer 通过 SceneRenderer 驱动
    }

    void Scene::OnRuntimeStart()
    {
        if (m_PhysicsBackend == PhysicsBackend::Custom)
        {
            m_PhysicsWorld = std::make_unique<PhysicsWorld>();
            m_PhysicsWorld->Init();
        }
        else
        {
            m_BulletPhysicsWorld = std::make_unique<BulletPhysicsWorld>();
            m_BulletPhysicsWorld->Init();
            m_BulletPhysicsWorld->CreateBodies(m_Registry);
        }
    }

    void Scene::OnRuntimeStop()
    {
        m_PhysicsWorld.reset();
        if (m_BulletPhysicsWorld)
        {
            m_BulletPhysicsWorld->Shutdown();
            m_BulletPhysicsWorld.reset();
        }
    }

    Ref<Scene> Scene::Copy(Ref<Scene> src)
    {
        auto newScene = CreateRef<Scene>();

        newScene->m_ViewportWidth = src->m_ViewportWidth;
        newScene->m_ViewportHeight = src->m_ViewportHeight;
        newScene->m_PhysicsBackend = src->m_PhysicsBackend;

        // 拷贝 ShadowSettings
        if (src->m_SceneRenderer)
            newScene->m_FallbackShadowSettings = src->m_SceneRenderer->GetShadowSystem().GetSettings();
        else
            newScene->m_FallbackShadowSettings = src->m_FallbackShadowSettings;

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

            if (srcReg.all_of<MeshRendererComponent>(srcEntity))
                newEntity.AddComponent<MeshRendererComponent>(srcReg.get<MeshRendererComponent>(srcEntity));

            if (srcReg.all_of<LightComponent>(srcEntity))
                newEntity.AddComponent<LightComponent>(srcReg.get<LightComponent>(srcEntity));

            if (srcReg.all_of<CameraComponent>(srcEntity))
                newEntity.AddComponent<CameraComponent>(srcReg.get<CameraComponent>(srcEntity));

            if (srcReg.all_of<RigidBodyComponent>(srcEntity))
                newEntity.AddComponent<RigidBodyComponent>(srcReg.get<RigidBodyComponent>(srcEntity));

            if (srcReg.all_of<BoxColliderComponent>(srcEntity))
                newEntity.AddComponent<BoxColliderComponent>(srcReg.get<BoxColliderComponent>(srcEntity));

            if (srcReg.all_of<SphereColliderComponent>(srcEntity))
                newEntity.AddComponent<SphereColliderComponent>(srcReg.get<SphereColliderComponent>(srcEntity));
        }

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

    // Skybox 委托
    void Scene::LoadSkybox(const std::vector<std::string>& facePaths)
    {
        if (m_SceneRenderer)
            m_SceneRenderer->GetSkyboxSystem().LoadSkybox(facePaths);
    }

    void Scene::ClearSkybox()
    {
        if (m_SceneRenderer)
            m_SceneRenderer->GetSkyboxSystem().ClearSkybox();
    }

    bool Scene::HasSkybox() const
    {
        if (m_SceneRenderer)
            return m_SceneRenderer->GetSkyboxSystem().HasSkybox();
        return false;
    }

    const std::vector<std::string>& Scene::GetSkyboxFacePaths() const
    {
        if (m_SceneRenderer)
            return m_SceneRenderer->GetSkyboxSystem().GetFacePaths();
        static std::vector<std::string> empty;
        return empty;
    }

    // Shadow 委托
    ShadowSettings& Scene::GetShadowSettings()
    {
        if (m_SceneRenderer)
            return m_SceneRenderer->GetShadowSystem().GetSettings();
        return m_FallbackShadowSettings;
    }

    void Scene::ResizeShadowMap(int resolution)
    {
        if (m_SceneRenderer)
            m_SceneRenderer->GetShadowSystem().ResizeShadowMap(resolution);
        m_FallbackShadowSettings.MapResolution = resolution;
    }

} // namespace Engine
