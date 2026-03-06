#include "engpch.h"
#include "Physics/BulletPhysicsWorld.h"
#include "Scene/Components.h"
#include "Terrain/TerrainMeshGenerator.h"
#include "Renderer/Mesh.h"
#include "Asset/AssetManager.h"
#include "Asset/PathUtils.h"
#include "Core/Log.h"

#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionShapes/btHeightfieldTerrainShape.h>
#include <BulletCollision/CollisionShapes/btConvexHullShape.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btTriangleMesh.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace Engine
{

    // GLM ↔ Bullet 转换辅助
    static btVector3 ToBt(const glm::vec3& v)
    {
        return btVector3(v.x, v.y, v.z);
    }

    static glm::vec3 ToGlm(const btVector3& v)
    {
        return glm::vec3(v.getX(), v.getY(), v.getZ());
    }

    static btQuaternion EulerToBtQuat(const glm::vec3& euler)
    {
        glm::quat q(euler);
        return btQuaternion(q.x, q.y, q.z, q.w);
    }

    static glm::vec3 BtQuatToEuler(const btQuaternion& q)
    {
        glm::quat gq(q.getW(), q.getX(), q.getY(), q.getZ());
        return glm::eulerAngles(gq);
    }

    // 从模型文件加载顶点和索引数据（用于 MeshCollider）
    static bool LoadMeshData(const std::string& filepath,
                             std::vector<glm::vec3>& outVertices,
                             std::vector<uint32_t>& outIndices)
    {
        Assimp::Importer importer;
        const std::string resolvedFilepath = PathUtils::ResolvePathString(filepath);
        unsigned int flags = aiProcess_Triangulate
                           | aiProcess_JoinIdenticalVertices;

        const aiScene* scene = importer.ReadFile(resolvedFilepath, flags);
        if (!scene || !scene->mRootNode)
        {
            ENGINE_CORE_ERROR("MeshCollider: 无法加载模型 '{0}': {1}", resolvedFilepath, importer.GetErrorString());
            return false;
        }

        uint32_t vertexOffset = 0;
        for (unsigned int m = 0; m < scene->mNumMeshes; m++)
        {
            aiMesh* aiM = scene->mMeshes[m];

            for (unsigned int v = 0; v < aiM->mNumVertices; v++)
            {
                outVertices.push_back({aiM->mVertices[v].x, aiM->mVertices[v].y, aiM->mVertices[v].z});
            }

            for (unsigned int f = 0; f < aiM->mNumFaces; f++)
            {
                aiFace& face = aiM->mFaces[f];
                for (unsigned int i = 0; i < face.mNumIndices; i++)
                    outIndices.push_back(vertexOffset + face.mIndices[i]);
            }

            vertexOffset += aiM->mNumVertices;
        }

        return !outVertices.empty();
    }

    BulletPhysicsWorld::BulletPhysicsWorld()
    {
    }

    BulletPhysicsWorld::~BulletPhysicsWorld()
    {
        Shutdown();
    }

    void BulletPhysicsWorld::Init(glm::vec3 gravity)
    {
        m_CollisionConfig = new btDefaultCollisionConfiguration();
        m_Dispatcher = new btCollisionDispatcher(m_CollisionConfig);
        m_Broadphase = new btDbvtBroadphase();
        m_Solver = new btSequentialImpulseConstraintSolver();
        m_DynamicsWorld = new btDiscreteDynamicsWorld(m_Dispatcher, m_Broadphase, m_Solver, m_CollisionConfig);
        m_DynamicsWorld->setGravity(ToBt(gravity));

        m_PreviousFrameContacts.clear();
        m_CurrentFrameContacts.clear();
    }

    void BulletPhysicsWorld::Shutdown()
    {
        DestroyBodies();

        delete m_DynamicsWorld;
        m_DynamicsWorld = nullptr;
        delete m_Solver;
        m_Solver = nullptr;
        delete m_Broadphase;
        m_Broadphase = nullptr;
        delete m_Dispatcher;
        m_Dispatcher = nullptr;
        delete m_CollisionConfig;
        m_CollisionConfig = nullptr;

        m_PreviousFrameContacts.clear();
        m_CurrentFrameContacts.clear();
    }

    bool BulletPhysicsWorld::IsEntityTrigger(entt::registry& reg, entt::entity entity)
    {
        if (reg.all_of<BoxColliderComponent>(entity))
            if (reg.get<BoxColliderComponent>(entity).IsTrigger)
                return true;
        if (reg.all_of<SphereColliderComponent>(entity))
            if (reg.get<SphereColliderComponent>(entity).IsTrigger)
                return true;
        if (reg.all_of<MeshColliderComponent>(entity))
            if (reg.get<MeshColliderComponent>(entity).IsTrigger)
                return true;
        return false;
    }

    void BulletPhysicsWorld::CreateBodies(entt::registry& reg)
    {
        if (!m_DynamicsWorld)
            return;

        auto view = reg.view<TransformComponent, RigidBodyComponent>();
        for (auto entity : view)
        {
            // 有 TerrainComponent 的实体由下方地形循环专门处理（使用 btHeightfieldTerrainShape）
            if (reg.all_of<TerrainComponent>(entity))
                continue;

            auto& transform = view.get<TransformComponent>(entity);
            auto& rb = view.get<RigidBodyComponent>(entity);

            uint32_t entityId = static_cast<uint32_t>(entity);

            // 确定碰撞形状
            btCollisionShape* shape = nullptr;
            btTriangleMesh* triangleMesh = nullptr;
            bool isTrigger = false;

            if (reg.all_of<MeshColliderComponent>(entity))
            {
                // MeshCollider 优先
                auto& mc = reg.get<MeshColliderComponent>(entity);
                isTrigger = mc.IsTrigger;

                // 确定网格数据来源
                std::string meshPath = mc.MeshPath;
                if (meshPath.empty() && reg.all_of<MeshRendererComponent>(entity))
                {
                    auto& mrc = reg.get<MeshRendererComponent>(entity);
                    if (mrc.Type == MeshType::Model)
                    {
                        auto* mesh = AssetManager::Get<Mesh>(mrc.MeshAsset);
                        if (mesh)
                            meshPath = mesh->GetModelPath();
                    }
                }

                std::vector<glm::vec3> vertices;
                std::vector<uint32_t> indices;

                bool loaded = false;
                if (!meshPath.empty())
                    loaded = LoadMeshData(meshPath, vertices, indices);

                if (loaded && !vertices.empty())
                {
                    if (mc.Type == MeshColliderComponent::ColliderType::Convex)
                    {
                        // Convex Hull（适用于动态和静态物体）
                        auto* convexShape = new btConvexHullShape();
                        for (const auto& v : vertices)
                        {
                            convexShape->addPoint(ToBt(v * transform.Scale), false);
                        }
                        convexShape->recalcLocalAabb();
                        shape = convexShape;
                    }
                    else
                    {
                        // Static BVH Triangle Mesh（仅适用于静态物体）
                        triangleMesh = new btTriangleMesh();
                        for (size_t i = 0; i + 2 < indices.size(); i += 3)
                        {
                            glm::vec3 v0 = vertices[indices[i]] * transform.Scale;
                            glm::vec3 v1 = vertices[indices[i + 1]] * transform.Scale;
                            glm::vec3 v2 = vertices[indices[i + 2]] * transform.Scale;
                            triangleMesh->addTriangle(ToBt(v0), ToBt(v1), ToBt(v2));
                        }
                        shape = new btBvhTriangleMeshShape(triangleMesh, true);
                    }
                }
                else
                {
                    ENGINE_CORE_WARN("MeshCollider: 无法加载网格数据，退回到默认盒碰撞体 (entity={0})", entityId);
                    shape = new btBoxShape(btVector3(0.5f, 0.5f, 0.5f));
                }
            }
            else if (reg.all_of<SphereColliderComponent>(entity))
            {
                auto& sphere = reg.get<SphereColliderComponent>(entity);
                isTrigger = sphere.IsTrigger;
                float maxScale = std::max({transform.Scale.x, transform.Scale.y, transform.Scale.z});
                shape = new btSphereShape(sphere.Radius * maxScale);
            }
            else if (reg.all_of<BoxColliderComponent>(entity))
            {
                auto& box = reg.get<BoxColliderComponent>(entity);
                isTrigger = box.IsTrigger;
                shape = new btBoxShape(ToBt(box.HalfExtents * transform.Scale));
            }
            else
            {
                // 没有碰撞器，使用默认的单位盒
                shape = new btBoxShape(btVector3(0.5f, 0.5f, 0.5f));
            }

            // 计算质量和惯性
            float mass = 0.0f;
            if (rb.Type == RigidBodyComponent::BodyType::Dynamic)
                mass = rb.Mass;

            // 静态三角网格不支持 Dynamic（Bullet 限制）
            if (reg.all_of<MeshColliderComponent>(entity))
            {
                auto& mc = reg.get<MeshColliderComponent>(entity);
                if (mc.Type == MeshColliderComponent::ColliderType::Static && mass > 0.0f)
                {
                    ENGINE_CORE_WARN("MeshCollider (Static) 不支持 Dynamic 刚体，强制设为 Static (entity={0})", entityId);
                    mass = 0.0f;
                }
            }

            btVector3 localInertia(0, 0, 0);
            if (mass > 0.0f)
                shape->calculateLocalInertia(mass, localInertia);

            // Motion State（桥接 Transform ↔ Bullet）
            btTransform startTransform;
            startTransform.setIdentity();
            startTransform.setOrigin(ToBt(transform.Translation));
            startTransform.setRotation(EulerToBtQuat(transform.Rotation));

            auto* motionState = new btDefaultMotionState(startTransform);

            btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, motionState, shape, localInertia);
            rbInfo.m_restitution = rb.Restitution;
            rbInfo.m_friction = rb.Friction;

            auto* body = new btRigidBody(rbInfo);

            // 存储 entity ID 到 Bullet userIndex（int 范围足够，entt::entity 为 uint32_t）
            static_assert(sizeof(entt::entity) <= sizeof(int), "entt::entity exceeds int range");
            body->setUserIndex(static_cast<int>(entityId));

            // Kinematic 设置
            if (rb.Type == RigidBodyComponent::BodyType::Kinematic)
            {
                body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
                body->setActivationState(DISABLE_DEACTIVATION);
            }

            // Trigger 设置：不产生物理响应，但仍然触发碰撞检测
            if (isTrigger)
            {
                body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
            }

            // 固定旋转
            if (rb.FixedRotation)
            {
                body->setAngularFactor(btVector3(0, 0, 0));
            }

            // 重力缩放
            body->setGravity(m_DynamicsWorld->getGravity() * rb.GravityScale);

            m_DynamicsWorld->addRigidBody(body);

            BodyInfo info;
            info.body = body;
            info.shape = shape;
            info.motionState = motionState;
            info.triangleMesh = triangleMesh;
            info.isTrigger = isTrigger;
            m_Bodies[entityId] = info;
        }

        // 地形碰撞体（Static, mass=0）
        auto terrainView = reg.view<TransformComponent, TerrainComponent>();
        for (auto entity : terrainView)
        {
            auto& transform = terrainView.get<TransformComponent>(entity);
            auto& terrain = terrainView.get<TerrainComponent>(entity);
            uint32_t entityId = static_cast<uint32_t>(entity);

            auto* meshData = terrain.RuntimeMeshData;
            if (!meshData || meshData->HeightData.empty())
                continue;

            auto* terrainShape = new btHeightfieldTerrainShape(
                meshData->HeightmapWidth,
                meshData->HeightmapHeight,
                meshData->HeightData.data(),   // float* 必须持续有效
                1.0f,                          // heightScale 参数（Bullet 内部乘数）
                meshData->MinHeight,
                meshData->MaxHeight,
                1,            // upAxis = Y
                PHY_FLOAT,
                true);        // flipQuadEdges

            // localScaling 缩放到世界尺寸
            float cellSizeX = terrain.TerrainSize / (meshData->HeightmapWidth - 1);
            float cellSizeZ = terrain.TerrainSize / (meshData->HeightmapHeight - 1);
            terrainShape->setLocalScaling(btVector3(cellSizeX, terrain.HeightScale, cellSizeZ));

            // btHeightfieldTerrainShape 中心在 (0, (min+max)/2, 0)
            float midH = (meshData->MinHeight + meshData->MaxHeight) * 0.5f * terrain.HeightScale;
            btTransform startTransform;
            startTransform.setIdentity();
            startTransform.setOrigin(ToBt(transform.Translation) + btVector3(0, midH, 0));

            auto* motionState = new btDefaultMotionState(startTransform);
            btRigidBody::btRigidBodyConstructionInfo rbInfo(0.0f, motionState, terrainShape, btVector3(0, 0, 0));
            rbInfo.m_friction = terrain.Friction;
            rbInfo.m_restitution = terrain.Restitution;

            auto* body = new btRigidBody(rbInfo);
            body->setUserIndex(static_cast<int>(entityId));
            m_DynamicsWorld->addRigidBody(body);

            BodyInfo info{body, terrainShape, motionState, nullptr, false};
            m_Bodies[entityId] = info;
        }
    }

    void BulletPhysicsWorld::Step(float dt, entt::registry& reg)
    {
        if (!m_DynamicsWorld)
            return;

        SyncFromECS(reg);  // Kinematic: ECS → Bullet（stepSimulation 前）
        m_DynamicsWorld->stepSimulation(dt, 10, 1.0f / 60.0f);
        SyncToECS(reg);
        CollectCollisionEvents(reg);
    }

    void BulletPhysicsWorld::CollectCollisionEvents(entt::registry& reg)
    {
        m_CollisionEvents.clear();

        if (!m_Dispatcher)
            return;

        // 收集当前帧所有碰撞对
        m_CurrentFrameContacts.clear();

        int numManifolds = m_Dispatcher->getNumManifolds();
        for (int i = 0; i < numManifolds; i++)
        {
            btPersistentManifold* manifold = m_Dispatcher->getManifoldByIndexInternal(i);
            const btCollisionObject* objA = manifold->getBody0();
            const btCollisionObject* objB = manifold->getBody1();

            int numContacts = manifold->getNumContacts();
            if (numContacts <= 0)
                continue;

            uint32_t idA = static_cast<uint32_t>(objA->getUserIndex());
            uint32_t idB = static_cast<uint32_t>(objB->getUserIndex());

            // 标准化碰撞对（小 ID 在前）
            auto key = std::make_pair(std::min(idA, idB), std::max(idA, idB));

            // 取第一个有效接触点的信息
            float maxImpulse = 0.0f;
            glm::vec3 bestContactPoint = {0, 0, 0};
            glm::vec3 bestContactNormal = {0, 0, 0};

            bool hasContact = false;
            for (int j = 0; j < numContacts; j++)
            {
                btManifoldPoint& pt = manifold->getContactPoint(j);
                // 只要距离够近就算碰撞（不仅看 impulse，因为 trigger 无 impulse）
                if (pt.getDistance() <= 0.0f)
                {
                    hasContact = true;
                    float impulse = pt.getAppliedImpulse();
                    if (impulse > maxImpulse || !hasContact)
                    {
                        maxImpulse = impulse;
                        bestContactPoint = ToGlm(pt.getPositionWorldOnB());
                        bestContactNormal = ToGlm(pt.m_normalWorldOnB);
                    }
                }
            }

            if (!hasContact)
                continue;

            // 判断是否为触发器碰撞
            auto entityA = static_cast<entt::entity>(idA);
            auto entityB = static_cast<entt::entity>(idB);
            bool isTrigger = false;
            if (reg.valid(entityA) && reg.valid(entityB))
                isTrigger = IsEntityTrigger(reg, entityA) || IsEntityTrigger(reg, entityB);

            ContactPairInfo pairInfo;
            pairInfo.ContactPoint = bestContactPoint;
            pairInfo.ContactNormal = bestContactNormal;
            pairInfo.Impulse = maxImpulse;
            pairInfo.IsTrigger = isTrigger;

            m_CurrentFrameContacts[key] = pairInfo;
        }

        // 比较 previous vs current 生成 Enter/Stay/Exit 事件

        // Enter + Stay：遍历当前帧碰撞对
        for (auto& [key, info] : m_CurrentFrameContacts)
        {
            CollisionEvent event;
            event.EntityA = static_cast<entt::entity>(key.first);
            event.EntityB = static_cast<entt::entity>(key.second);
            event.ContactPoint = info.ContactPoint;
            event.ContactNormal = info.ContactNormal;
            event.Impulse = info.Impulse;
            event.IsTrigger = info.IsTrigger;

            if (m_PreviousFrameContacts.find(key) == m_PreviousFrameContacts.end())
            {
                event.Type = CollisionEventType::Enter;
            }
            else
            {
                event.Type = CollisionEventType::Stay;
            }

            m_CollisionEvents.push_back(event);
        }

        // Exit：在上一帧有但当前帧没有的碰撞对
        for (auto& [key, info] : m_PreviousFrameContacts)
        {
            if (m_CurrentFrameContacts.find(key) == m_CurrentFrameContacts.end())
            {
                CollisionEvent event;
                event.Type = CollisionEventType::Exit;
                event.EntityA = static_cast<entt::entity>(key.first);
                event.EntityB = static_cast<entt::entity>(key.second);
                event.ContactPoint = {0, 0, 0};
                event.ContactNormal = {0, 0, 0};
                event.Impulse = 0.0f;
                event.IsTrigger = info.IsTrigger;

                m_CollisionEvents.push_back(event);
            }
        }

        // 交换：当前帧变为上一帧
        m_PreviousFrameContacts = m_CurrentFrameContacts;
    }

    void BulletPhysicsWorld::SyncFromECS(entt::registry& reg)
    {
        for (auto& [entityId, info] : m_Bodies)
        {
            auto entity = static_cast<entt::entity>(entityId);
            if (!reg.valid(entity))
                continue;

            if (!reg.all_of<TransformComponent, RigidBodyComponent>(entity))
                continue;

            auto& rb = reg.get<RigidBodyComponent>(entity);
            if (rb.Type != RigidBodyComponent::BodyType::Kinematic)
                continue;

            auto& transform = reg.get<TransformComponent>(entity);
            btTransform btTrans;
            btTrans.setIdentity();
            btTrans.setOrigin(ToBt(transform.Translation));
            btTrans.setRotation(EulerToBtQuat(transform.Rotation));
            info.motionState->setWorldTransform(btTrans);
        }
    }

    void BulletPhysicsWorld::SyncToECS(entt::registry& reg)
    {
        for (auto& [entityId, info] : m_Bodies)
        {
            auto entity = static_cast<entt::entity>(entityId);
            if (!reg.valid(entity))
                continue;

            if (!reg.all_of<TransformComponent, RigidBodyComponent>(entity))
                continue;

            auto& rb = reg.get<RigidBodyComponent>(entity);
            if (rb.Type != RigidBodyComponent::BodyType::Dynamic)
                continue;

            auto& transform = reg.get<TransformComponent>(entity);

            btTransform btTrans;
            info.motionState->getWorldTransform(btTrans);

            transform.Translation = ToGlm(btTrans.getOrigin());
            if (!rb.FixedRotation)
                transform.Rotation = BtQuatToEuler(btTrans.getRotation());

            // 同步速度到 ECS 组件
            rb.LinearVelocity = ToGlm(info.body->getLinearVelocity());
            rb.AngularVelocity = ToGlm(info.body->getAngularVelocity());
        }
    }

    void BulletPhysicsWorld::DestroyBody(entt::entity entity)
    {
        if (!m_DynamicsWorld)
            return;

        uint32_t entityId = static_cast<uint32_t>(entity);
        auto it = m_Bodies.find(entityId);
        if (it == m_Bodies.end())
            return;

        auto& info = it->second;
        if (info.body)
        {
            m_DynamicsWorld->removeRigidBody(info.body);
            delete info.body;
        }
        delete info.shape;
        delete info.motionState;
        delete info.triangleMesh;
        m_Bodies.erase(it);
    }

    void BulletPhysicsWorld::DestroyBodies()
    {
        if (!m_DynamicsWorld)
            return;

        for (auto& [id, info] : m_Bodies)
        {
            if (info.body)
            {
                m_DynamicsWorld->removeRigidBody(info.body);
                delete info.body;
            }
            delete info.shape;
            delete info.motionState;
            delete info.triangleMesh;
        }
        m_Bodies.clear();
    }

} // namespace Engine
