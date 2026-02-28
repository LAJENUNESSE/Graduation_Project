#include "engpch.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Script/NativeScriptComponent.h"
#include "Reflection/ComponentRegistry.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/EditorCamera.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/BulletPhysicsWorld.h"
#include "Terrain/TerrainMeshGenerator.h"

namespace Engine
{

    Scene::Scene()
    {
        ComponentRegistry::EnsureRegistered();
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
        // NativeScript OnUpdate（物理之前）
        {
            auto view = m_Registry.view<NativeScriptComponent>();
            for (auto entity : view)
            {
                auto& nsc = view.get<NativeScriptComponent>(entity);
                if (nsc.Instance)
                    nsc.Instance->OnUpdate(ts);
            }
        }

        // Physics step
        if (m_PhysicsBackend == PhysicsBackend::Custom && m_PhysicsWorld)
            m_PhysicsWorld->Step(ts, m_Registry);
        else if (m_PhysicsBackend == PhysicsBackend::Bullet && m_BulletPhysicsWorld)
        {
            m_BulletPhysicsWorld->Step(ts, m_Registry);

            // 碰撞触发粒子爆发
            for (const auto& event : m_BulletPhysicsWorld->GetCollisionEvents())
            {
                auto tryTriggerBurst = [&](entt::entity entity, const glm::vec3& normal) {
                    if (!m_Registry.valid(entity)) return;
                    if (!m_Registry.all_of<CollisionParticleTriggerComponent, ParticleEmitterComponent>(entity)) return;

                    auto& trigger = m_Registry.get<CollisionParticleTriggerComponent>(entity);
                    auto& emitter = m_Registry.get<ParticleEmitterComponent>(entity);

                    if (!trigger.Enabled) return;
                    if (event.Impulse < trigger.MinImpulse) return;

                    // 按冲量比例缩放爆发数（MinImpulse 最小 0.001 防除零）
                    float safeMinImpulse = std::max(trigger.MinImpulse, 0.001f);
                    float scale = std::min(event.Impulse / safeMinImpulse, 5.0f);
                    int burst = static_cast<int>(trigger.BurstOnCollision * scale);
                    emitter.CollisionBurstCount += burst;

                    // 限制总碰撞爆发不超过粒子池容量
                    emitter.CollisionBurstCount = std::min(emitter.CollisionBurstCount,
                        static_cast<int>(emitter.MaxParticles));

                    if (trigger.UseCollisionNormal)
                        emitter.EmitDirection = normal;
                };

                tryTriggerBurst(event.EntityA, event.ContactNormal);
                tryTriggerBurst(event.EntityB, -event.ContactNormal);
            }
        }

        // Audio + Video 系统更新（物理之后）
        if (m_SceneRenderer)
        {
            m_SceneRenderer->GetAudioSystem().OnUpdate(m_Registry, ts);
            m_SceneRenderer->GetVideoSystem().OnUpdate(m_Registry, ts);
        }

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

            // 在创建物理体之前，确保地形网格数据已生成（Scene::Copy 会清空 RuntimeMeshData）
            {
                auto terrainView = m_Registry.view<TransformComponent, TerrainComponent>();
                for (auto entity : terrainView)
                {
                    auto& tc = m_Registry.get<TerrainComponent>(entity);
                    if (!tc.RuntimeMeshData && !tc.HeightmapPath.empty())
                    {
                        auto meshData = TerrainMeshGenerator::Generate(
                            tc.HeightmapPath, tc.TerrainSize, tc.HeightScale, tc.LODLevels);
                        tc.RuntimeMeshData = new TerrainMeshData(std::move(meshData));
                        tc.MeshDirty = false;
                    }
                }
            }

            m_BulletPhysicsWorld->CreateBodies(m_Registry);
        }

        // NativeScript OnCreate
        {
            auto view = m_Registry.view<NativeScriptComponent>();
            for (auto entity : view)
            {
                auto& nsc = view.get<NativeScriptComponent>(entity);
                if (nsc.InstantiateScript)
                {
                    nsc.InstantiateScript(nsc);
                    if (nsc.Instance)
                    {
                        nsc.Instance->m_Entity = Entity{entity, this};
                        nsc.Instance->OnCreate();
                    }
                }
            }
        }

        // Audio + Video 系统启动
        if (m_SceneRenderer)
        {
            m_SceneRenderer->GetAudioSystem().OnRuntimeStart(m_Registry);
            m_SceneRenderer->GetVideoSystem().OnRuntimeStart(m_Registry);
        }
    }

    void Scene::OnRuntimeStop()
    {
        // Audio + Video 系统停止（先于脚本销毁）
        if (m_SceneRenderer)
        {
            m_SceneRenderer->GetVideoSystem().OnRuntimeStop(m_Registry);
            m_SceneRenderer->GetAudioSystem().OnRuntimeStop(m_Registry);
        }

        // NativeScript OnDestroy
        {
            auto view = m_Registry.view<NativeScriptComponent>();
            for (auto entity : view)
            {
                auto& nsc = view.get<NativeScriptComponent>(entity);
                if (nsc.Instance)
                {
                    nsc.Instance->OnDestroy();
                    nsc.Instance.reset();
                }
            }
        }

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
            {
                auto mrc = srcReg.get<MeshRendererComponent>(srcEntity);
                mrc.CachedMaterials.clear();  // 避免拷贝场景与原场景共享 Material
                newEntity.AddComponent<MeshRendererComponent>(mrc);
            }

            if (srcReg.all_of<CameraComponent>(srcEntity))
                newEntity.AddComponent<CameraComponent>(srcReg.get<CameraComponent>(srcEntity));

            // 反射注册的组件通过 ComponentRegistry 自动拷贝
            uint32_t srcId = static_cast<uint32_t>(srcEntity);
            uint32_t dstId = static_cast<uint32_t>(static_cast<entt::entity>(newEntity));
            for (auto& meta : ComponentRegistry::Instance().GetAll())
            {
                if (meta.Has(*src, srcId))
                    meta.Copy(*src, srcId, *newScene, dstId);
            }

            if (srcReg.all_of<TerrainComponent>(srcEntity))
            {
                auto tc = srcReg.get<TerrainComponent>(srcEntity);
                tc.RuntimeMeshData = nullptr;  // 清除运行时指针
                tc.MeshDirty = true;           // 新场景重建网格
                newEntity.AddComponent<TerrainComponent>(tc);
            }

            if (srcReg.all_of<ParticleEmitterComponent>(srcEntity))
            {
                auto pe = srcReg.get<ParticleEmitterComponent>(srcEntity);
                pe.RuntimeParticleSystem = nullptr;  // 清除裸指针，避免双重释放
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

            // AudioSourceComponent: 拷贝配置，清除运行时状态
            if (srcReg.all_of<AudioSourceComponent>(srcEntity))
            {
                auto asc = srcReg.get<AudioSourceComponent>(srcEntity);
                asc.RuntimeSource = 0;
                asc.RuntimeBuffer = 0;
                asc.IsPlaying = false;
                newEntity.AddComponent<AudioSourceComponent>(asc);
            }

            // AudioListenerComponent
            if (srcReg.all_of<AudioListenerComponent>(srcEntity))
                newEntity.AddComponent<AudioListenerComponent>(srcReg.get<AudioListenerComponent>(srcEntity));

            // VideoPlayerComponent: 拷贝配置，清除运行时状态
            if (srcReg.all_of<VideoPlayerComponent>(srcEntity))
            {
                auto vpc = srcReg.get<VideoPlayerComponent>(srcEntity);
                vpc.RuntimeDecoder = nullptr;
                vpc.RuntimeAudioSource = 0;
                vpc.RuntimeTexture = nullptr;
                vpc.RuntimeAudioBuffers.clear();
                vpc.IsPlaying = false;
                newEntity.AddComponent<VideoPlayerComponent>(vpc);
            }
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
