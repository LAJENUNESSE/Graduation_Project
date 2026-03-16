#include "engpch.h"
#include "Scene/Runtime/SceneRuntimeCoordinator.h"
#include "Core/Log.h"
#include "Physics/BulletPhysicsWorld.h"
#include "Physics/PhysicsWorld.h"
#include "Renderer/SceneRenderer.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scene/SceneEntityIndex.h"
#include "Scene/Runtime/ResourceLifecycleCoordinator.h"
#include "Scene/Runtime/RuntimeComponents.h"
#include "Script/NativeScriptComponent.h"
#include "Script/ScriptableEntity.h"
#include "Terrain/TerrainMeshGenerator.h"

#include <set>

namespace Engine
{

    SceneRuntimeCoordinator::SceneRuntimeCoordinator(entt::registry& reg, SceneEntityIndex& index,
                                                     ResourceLifecycleCoordinator& lifecycle, Scene* scene)
        : m_Registry(reg), m_EntityIndex(index), m_Lifecycle(lifecycle), m_Scene(scene)
    {
    }

    SceneRuntimeCoordinator::~SceneRuntimeCoordinator() = default;

    void SceneRuntimeCoordinator::DestroyPhysicsBody(entt::entity entity)
    {
        if (m_BulletPhysicsWorld)
            m_BulletPhysicsWorld->DestroyBody(entity);
    }

    void SceneRuntimeCoordinator::OnRuntimeStart(PhysicsBackend backend, SceneRenderer* renderer)
    {
        const char* backendName = backend == PhysicsBackend::Custom ? "Custom" : "Bullet";
        size_t entityCount = 0;
        for (auto entity : m_Registry.view<IDComponent>())
        {
            (void)entity;
            ++entityCount;
        }
        ENGINE_CORE_INFO("[SceneLifecycle] RuntimeStart backend={0}, entities={1}", backendName, entityCount);

        if (backend == PhysicsBackend::Custom)
        {
            m_PhysicsWorld = std::make_unique<PhysicsWorld>();
            m_PhysicsWorld->Init();
        }
        else
        {
            m_BulletPhysicsWorld = std::make_unique<BulletPhysicsWorld>();
            m_BulletPhysicsWorld->Init();

            // 在创建物理体之前，确保地形网格数据已生成（Scene::Copy 不拷贝 runtime 组件）
            {
                auto terrainView = m_Registry.view<TransformComponent, TerrainComponent>();
                for (auto entity : terrainView)
                {
                    auto& tc = m_Registry.get<TerrainComponent>(entity);
                    bool hasRuntimeMesh = m_Registry.all_of<TerrainRuntimeComponent>(entity) &&
                                          m_Registry.get<TerrainRuntimeComponent>(entity).MeshData;
                    if (!hasRuntimeMesh && !tc.HeightmapPath.empty())
                    {
                        auto meshData = TerrainMeshGenerator::Generate(tc.HeightmapPath, tc.TerrainSize, tc.HeightScale,
                                                                       tc.LODLevels);
                        auto& rtc = m_Registry.get_or_emplace<TerrainRuntimeComponent>(entity);
                        rtc.MeshData = std::make_unique<TerrainMeshData>(std::move(meshData));
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
                        nsc.Instance->m_Entity = Entity{entity, m_Scene};
                        nsc.Instance->OnCreate();
                    }
                }
            }
        }

        // 注册 lifecycle coordinator 清理回调
        m_Lifecycle.ClearAll();
        m_Lifecycle.RegisterEntityCleanup([](entt::registry& reg, entt::entity e)
        {
            if (reg.all_of<TerrainRuntimeComponent>(e))
                reg.remove<TerrainRuntimeComponent>(e);
        });
        if (renderer)
        {
            m_Lifecycle.RegisterEntityCleanup([renderer](entt::registry& reg, entt::entity e)
            {
                if (reg.all_of<AudioSourceComponent>(e))
                    renderer->GetAudioSystem().DestroyEntityAudio(static_cast<uint32_t>(e));
            });
            m_Lifecycle.RegisterEntityCleanup([renderer](entt::registry& reg, entt::entity e)
            {
                if (reg.all_of<VideoPlayerComponent>(e))
                    renderer->GetVideoSystem().DestroyEntityVideo(static_cast<uint32_t>(e));
            });
            m_Lifecycle.RegisterEntityCleanup([renderer](entt::registry& reg, entt::entity e)
            {
                uint32_t eid = static_cast<uint32_t>(e);
                if (reg.all_of<ParticleEmitterComponent>(e))
                    renderer->ReleaseParticleSystem(eid);
                if (reg.all_of<FluidEmitterComponent>(e))
                    renderer->ReleaseFluidSystem(eid);
            });
        }

        // Audio + Video 系统启动
        if (renderer)
        {
            renderer->GetAudioSystem().OnRuntimeStart(m_Registry);
            renderer->GetVideoSystem().OnRuntimeStart(m_Registry);
        }
    }

    void SceneRuntimeCoordinator::OnRuntimeStop(SceneRenderer* renderer)
    {
        size_t entityCount = 0;
        for (auto entity : m_Registry.view<IDComponent>())
        {
            (void)entity;
            ++entityCount;
        }
        ENGINE_CORE_INFO("[SceneLifecycle] RuntimeStop entities={0}", entityCount);

        // Audio + Video 系统停止（先于脚本销毁）
        if (renderer)
        {
            renderer->GetVideoSystem().OnRuntimeStop(m_Registry);
            renderer->GetAudioSystem().OnRuntimeStop(m_Registry);
        }

        m_Lifecycle.CleanupRuntimeStop(m_Registry);

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

    void SceneRuntimeCoordinator::OnUpdateRuntime(Timestep ts, PhysicsBackend backend, SceneRenderer* renderer)
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
        if (backend == PhysicsBackend::Custom && m_PhysicsWorld)
            m_PhysicsWorld->Step(ts, m_Registry);
        else if (backend == PhysicsBackend::Bullet && m_BulletPhysicsWorld)
        {
            m_BulletPhysicsWorld->Step(ts, m_Registry);

            const auto& events = m_BulletPhysicsWorld->GetCollisionEvents();
            std::set<std::pair<uint32_t, uint32_t>> processedParticlePairs;
            ProcessCollisionParticleBursts(events, processedParticlePairs);
            DispatchCollisionCallbacks(events);
        }

        // Audio + Video 系统更新（物理之后）
        if (renderer)
        {
            renderer->GetAudioSystem().OnUpdate(m_Registry, ts);
            renderer->GetVideoSystem().OnUpdate(m_Registry, ts);
        }
    }

    void SceneRuntimeCoordinator::ProcessCollisionParticleBursts(
        const std::vector<CollisionEvent>& events,
        std::set<std::pair<uint32_t, uint32_t>>& processedPairs)
    {
        for (const auto& event : events)
        {
            if (event.Type != CollisionEventType::Enter)
                continue;

            auto tryTriggerBurst =
                [&](entt::entity triggerEntity, entt::entity otherEntity, const glm::vec3& normal)
            {
                if (!m_Registry.valid(triggerEntity))
                    return;
                if (!m_Registry.all_of<CollisionParticleTriggerComponent, ParticleEmitterComponent>(triggerEntity))
                    return;

                // entity pair 去重
                uint32_t a = static_cast<uint32_t>(triggerEntity);
                uint32_t b = static_cast<uint32_t>(otherEntity);
                auto key = std::make_pair(std::min(a, b), std::max(a, b));
                if (processedPairs.count(key))
                    return;
                processedPairs.insert(key);

                auto& trigger = m_Registry.get<CollisionParticleTriggerComponent>(triggerEntity);
                auto& emitter = m_Registry.get<ParticleEmitterComponent>(triggerEntity);

                if (!trigger.Enabled)
                    return;
                if (event.Impulse < trigger.MinImpulse)
                    return;

                // 按冲量比例缩放爆发数（MinImpulse 最小 0.001 防除零）
                float safeMinImpulse = std::max(trigger.MinImpulse, 0.001f);
                float scale = std::min(event.Impulse / safeMinImpulse, 5.0f);
                int burst = static_cast<int>(trigger.BurstOnCollision * scale);
                emitter.CollisionBurstCount += burst;

                // 限制每帧碰撞爆发数
                emitter.CollisionBurstCount = std::min(emitter.CollisionBurstCount, trigger.MaxBurstPerFrame);
                // 限制总碰撞爆发不超过粒子池容量
                emitter.CollisionBurstCount =
                    std::min(emitter.CollisionBurstCount, static_cast<int>(emitter.MaxParticles));

                if (trigger.UseCollisionNormal)
                    emitter.EmitDirection = normal;
            };

            tryTriggerBurst(event.EntityA, event.EntityB, event.ContactNormal);
            tryTriggerBurst(event.EntityB, event.EntityA, -event.ContactNormal);
        }
    }

    void SceneRuntimeCoordinator::DispatchCollisionCallbacks(const std::vector<CollisionEvent>& events)
    {
        for (const auto& event : events)
        {
            auto dispatchCallback =
                [&](entt::entity selfEntity, entt::entity otherEntity, const glm::vec3& contactNormal)
            {
                if (!m_Registry.valid(selfEntity))
                    return;
                if (!m_Registry.all_of<NativeScriptComponent>(selfEntity))
                    return;

                auto& nsc = m_Registry.get<NativeScriptComponent>(selfEntity);
                if (!nsc.Instance)
                    return;

                Entity otherWrapped = {otherEntity, m_Scene};

                if (event.IsTrigger)
                {
                    // 触发器回调
                    if (event.Type == CollisionEventType::Enter)
                        nsc.Instance->OnTriggerEnter(otherWrapped);
                    else if (event.Type == CollisionEventType::Exit)
                        nsc.Instance->OnTriggerExit(otherWrapped);
                }
                else
                {
                    // 物理碰撞回调
                    if (event.Type == CollisionEventType::Enter)
                    {
                        CollisionCallbackInfo info;
                        info.OtherEntity = otherWrapped;
                        info.ContactPoint = event.ContactPoint;
                        info.ContactNormal = contactNormal;
                        info.Impulse = event.Impulse;
                        nsc.Instance->OnCollisionEnter(info);
                    }
                    else if (event.Type == CollisionEventType::Stay)
                    {
                        CollisionCallbackInfo info;
                        info.OtherEntity = otherWrapped;
                        info.ContactPoint = event.ContactPoint;
                        info.ContactNormal = contactNormal;
                        info.Impulse = event.Impulse;
                        nsc.Instance->OnCollisionStay(info);
                    }
                    else if (event.Type == CollisionEventType::Exit)
                    {
                        nsc.Instance->OnCollisionExit(otherWrapped);
                    }
                }
            };

            dispatchCallback(event.EntityA, event.EntityB, event.ContactNormal);
            dispatchCallback(event.EntityB, event.EntityA, -event.ContactNormal);
        }
    }

} // namespace Engine
