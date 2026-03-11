#include "engpch.h"
#include "Scene/Scene.h"
#include "Core/Log.h"
#include "Physics/BulletPhysicsWorld.h"
#include "Physics/PhysicsWorld.h"
#include "Reflection/ComponentPolicies.h"
#include "Reflection/ComponentRegistry.h"
#include "Renderer/EditorCamera.h"
#include "Renderer/SceneRenderer.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Script/NativeScriptComponent.h"
#include "Script/ScriptableEntity.h"
#include "Terrain/TerrainMeshGenerator.h"

#include <set>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Engine
{

    Scene::Scene()
    {
        ComponentRegistry::EnsureRegistered();
    }

    Scene::~Scene() {}

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

        if (m_BulletPhysicsWorld)
            m_BulletPhysicsWorld->DestroyBody((entt::entity)entity);

        m_Registry.destroy(entity);
    }

    void Scene::SetParent(Entity child, Entity parent)
    {
        if (!child || !parent)
            return;
        if (child == parent)
            return;

        // 防止循环：parent 不能是 child 的后代
        if (IsAncestorOf(child, parent))
            return;

        // 1. 记录子实体当前世界变换
        glm::mat4 childWorldMatrix = GetWorldTransform(child);

        // 2. 先解除旧的父子关系（不调用 RemoveParent 以避免变换转换）
        if (child.HasComponent<RelationshipComponent>())
        {
            auto& oldChildRel = child.GetComponent<RelationshipComponent>();
            if (static_cast<uint64_t>(oldChildRel.ParentID) != 0)
            {
                Entity oldParent = FindEntityByUUID(oldChildRel.ParentID);
                if (oldParent && oldParent.HasComponent<RelationshipComponent>())
                {
                    auto& oldParentChildren = oldParent.GetComponent<RelationshipComponent>().Children;
                    UUID childUUID = child.GetUUID();
                    oldParentChildren.erase(
                        std::remove_if(oldParentChildren.begin(), oldParentChildren.end(), [childUUID](UUID id)
                                       { return static_cast<uint64_t>(id) == static_cast<uint64_t>(childUUID); }),
                        oldParentChildren.end());
                }
                oldChildRel.ParentID = 0;
            }
        }

        // 3. 建立新的父子关系
        auto& childRel = child.GetComponent<RelationshipComponent>();
        auto& parentRel = parent.GetComponent<RelationshipComponent>();

        childRel.ParentID = parent.GetUUID();
        parentRel.Children.push_back(child.GetUUID());

        // 4. 计算新父物体的世界变换，将子物体世界变换转为相对于新父的本地变换
        glm::mat4 parentWorldMatrix = GetWorldTransform(parent);
        glm::mat4 childLocalMatrix = glm::inverse(parentWorldMatrix) * childWorldMatrix;

        // 5. 从本地矩阵分解出 Translation / Rotation / Scale 写回子实体
        glm::vec3 scale;
        glm::quat rotation;
        glm::vec3 translation;
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(childLocalMatrix, scale, rotation, translation, skew, perspective);

        auto& childTransform = child.GetComponent<TransformComponent>();
        childTransform.Translation = translation;
        childTransform.Rotation = glm::eulerAngles(rotation);
        childTransform.Scale = scale;
    }

    void Scene::RemoveParent(Entity child)
    {
        if (!child || !child.HasComponent<RelationshipComponent>())
            return;

        auto& childRel = child.GetComponent<RelationshipComponent>();
        if (static_cast<uint64_t>(childRel.ParentID) == 0)
            return;

        // 1. 记录子实体当前世界变换（解除前还有父物体）
        glm::mat4 childWorldMatrix = GetWorldTransform(child);

        // 2. 从父实体的 Children 列表中移除自身
        Entity parent = FindEntityByUUID(childRel.ParentID);
        if (parent && parent.HasComponent<RelationshipComponent>())
        {
            auto& parentChildren = parent.GetComponent<RelationshipComponent>().Children;
            UUID childUUID = child.GetUUID();
            parentChildren.erase(
                std::remove_if(parentChildren.begin(), parentChildren.end(), [childUUID](UUID id)
                               { return static_cast<uint64_t>(id) == static_cast<uint64_t>(childUUID); }),
                parentChildren.end());
        }

        childRel.ParentID = 0;

        // 3. 解除后成为根节点，世界变换 = 本地变换，需把之前的世界变换写回
        glm::vec3 scale;
        glm::quat rotation;
        glm::vec3 translation;
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(childWorldMatrix, scale, rotation, translation, skew, perspective);

        auto& childTransform = child.GetComponent<TransformComponent>();
        childTransform.Translation = translation;
        childTransform.Rotation = glm::eulerAngles(rotation);
        childTransform.Scale = scale;
    }

    std::vector<Entity> Scene::GetChildren(Entity parent)
    {
        std::vector<Entity> result;
        if (!parent || !parent.HasComponent<RelationshipComponent>())
            return result;

        auto& rel = parent.GetComponent<RelationshipComponent>();
        for (auto childUUID : rel.Children)
        {
            Entity child = FindEntityByUUID(childUUID);
            if (child)
                result.push_back(child);
        }
        return result;
    }

    Entity Scene::FindEntityByUUID(UUID uuid)
    {
        auto view = m_Registry.view<IDComponent>();
        for (auto entity : view)
        {
            if (view.get<IDComponent>(entity).ID == uuid)
                return Entity{entity, this};
        }
        return {};
    }

    bool Scene::IsAncestorOf(Entity ancestor, Entity entity)
    {
        if (!entity || !entity.HasComponent<RelationshipComponent>())
            return false;

        auto& rel = entity.GetComponent<RelationshipComponent>();
        if (static_cast<uint64_t>(rel.ParentID) == 0)
            return false;

        if (rel.ParentID == ancestor.GetUUID())
            return true;

        Entity parent = FindEntityByUUID(rel.ParentID);
        return parent ? IsAncestorOf(ancestor, parent) : false;
    }

    glm::mat4 Scene::GetWorldTransform(Entity entity)
    {
        if (!entity || !entity.HasComponent<TransformComponent>())
            return glm::mat4(1.0f);

        glm::mat4 localTransform = entity.GetComponent<TransformComponent>().GetTransform();

        if (entity.HasComponent<RelationshipComponent>())
        {
            auto& rel = entity.GetComponent<RelationshipComponent>();
            if (static_cast<uint64_t>(rel.ParentID) != 0)
            {
                Entity parent = FindEntityByUUID(rel.ParentID);
                if (parent)
                    return GetWorldTransform(parent) * localTransform;
            }
        }

        return localTransform;
    }

    std::vector<Entity> Scene::GetRootEntities()
    {
        std::vector<Entity> roots;
        auto view = m_Registry.view<IDComponent, RelationshipComponent>();
        for (auto entity : view)
        {
            auto& rel = view.get<RelationshipComponent>(entity);
            if (static_cast<uint64_t>(rel.ParentID) == 0)
                roots.push_back(Entity{entity, this});
        }
        return roots;
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

            // 碰撞触发粒子爆发 + 碰撞回调分发
            std::set<std::pair<uint32_t, uint32_t>> processedParticlePairs;
            for (const auto& event : m_BulletPhysicsWorld->GetCollisionEvents())
            {
                // 1) 碰撞粒子触发（仅 Enter 事件）
                if (event.Type == CollisionEventType::Enter)
                {
                    auto tryTriggerBurst =
                        [&](entt::entity triggerEntity, entt::entity otherEntity, const glm::vec3& normal)
                    {
                        if (!m_Registry.valid(triggerEntity))
                            return;
                        if (!m_Registry.all_of<CollisionParticleTriggerComponent, ParticleEmitterComponent>(
                                triggerEntity))
                            return;

                        // entity pair 去重
                        uint32_t a = static_cast<uint32_t>(triggerEntity);
                        uint32_t b = static_cast<uint32_t>(otherEntity);
                        auto key = std::make_pair(std::min(a, b), std::max(a, b));
                        if (processedParticlePairs.count(key))
                            return;
                        processedParticlePairs.insert(key);

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

                // 2) 碰撞回调分发到 NativeScript
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

                    Entity otherWrapped = {otherEntity, this};

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
        const char* backendName = m_PhysicsBackend == PhysicsBackend::Custom ? "Custom" : "Bullet";
        size_t entityCount = 0;
        for (auto entity : m_Registry.view<IDComponent>())
        {
            (void)entity;
            ++entityCount;
        }
        ENGINE_CORE_INFO("[SceneLifecycle] RuntimeStart backend={0}, entities={1}", backendName, entityCount);

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
                        auto meshData = TerrainMeshGenerator::Generate(tc.HeightmapPath, tc.TerrainSize, tc.HeightScale,
                                                                       tc.LODLevels);
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
        size_t entityCount = 0;
        for (auto entity : m_Registry.view<IDComponent>())
        {
            (void)entity;
            ++entityCount;
        }
        ENGINE_CORE_INFO("[SceneLifecycle] RuntimeStop entities={0}", entityCount);

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

            // 反射注册的组件通过 ComponentRegistry 自动拷贝
            uint32_t srcId = static_cast<uint32_t>(srcEntity);
            uint32_t dstId = static_cast<uint32_t>(static_cast<entt::entity>(newEntity));
            for (auto& meta : ComponentRegistry::Instance().GetAll())
            {
                if (ComponentPolicies::IsCustomCopyComponentType(meta.TypeName))
                    continue;

                if (meta.Has(*src, srcId))
                    meta.Copy(*src, srcId, *newScene, dstId);
            }

            if (srcReg.all_of<TerrainComponent>(srcEntity))
            {
                auto tc = srcReg.get<TerrainComponent>(srcEntity);
                tc.RuntimeMeshData = nullptr; // 清除运行时指针
                tc.MeshDirty = true;          // 新场景重建网格
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

            // VideoPlayerComponent: 拷贝配置，清除运行时状态（move-only，手动构建）
            if (srcReg.all_of<VideoPlayerComponent>(srcEntity))
            {
                auto& srcVpc = srcReg.get<VideoPlayerComponent>(srcEntity);
                auto& dstVpc = newEntity.AddComponent<VideoPlayerComponent>();
                dstVpc.StreamURL = srcVpc.StreamURL;
                dstVpc.PlayOnStart = srcVpc.PlayOnStart;
                dstVpc.Loop = srcVpc.Loop;
                dstVpc.Volume = srcVpc.Volume;
                // 运行时状态保持默认（nullptr / 0 / false）
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
