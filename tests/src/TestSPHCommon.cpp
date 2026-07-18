#include <gtest/gtest.h>
#include "Renderer/SPHCommon.h"
#include "Scene/Components.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <entt/entt.hpp>

using namespace Engine;

// ============================================================================
// CollectRigidBodies 测试
// ============================================================================

TEST(CollectRigidBodies, NullRegistry_ReturnsEmpty)
{
    auto bodies = CollectRigidBodies(nullptr, 64, RigidBodyUploadFilter::AllColliders);
    EXPECT_TRUE(bodies.empty());
}

TEST(CollectRigidBodies, EmptyRegistry_ReturnsEmpty)
{
    entt::registry registry;
    auto bodies = CollectRigidBodies(&registry, 64, RigidBodyUploadFilter::AllColliders);
    EXPECT_TRUE(bodies.empty());
}

// ---- Box collider ----

TEST(CollectRigidBodies, SingleBoxCollider)
{
    entt::registry registry;
    auto entity = registry.create();
    registry.emplace<TransformComponent>(entity, glm::vec3(1.0f, 2.0f, 3.0f));
    registry.emplace<BoxColliderComponent>(entity);

    auto bodies = CollectRigidBodies(&registry, 64, RigidBodyUploadFilter::AllColliders);
    ASSERT_EQ(bodies.size(), 1u);
    // posAndType.w = 0 for box
    EXPECT_FLOAT_EQ(bodies[0].posAndType.w, 0.0f);
    // 位置应为 Translation + rot * (Offset * Scale) = (1,2,3) + identity * (0 * 1) = (1,2,3)
    EXPECT_FLOAT_EQ(bodies[0].posAndType.x, 1.0f);
    EXPECT_FLOAT_EQ(bodies[0].posAndType.y, 2.0f);
    EXPECT_FLOAT_EQ(bodies[0].posAndType.z, 3.0f);
    // 默认 HalfExtents = (0.5, 0.5, 0.5), Scale = (1,1,1)
    EXPECT_FLOAT_EQ(bodies[0].halfExtents.x, 0.5f);
    EXPECT_FLOAT_EQ(bodies[0].halfExtents.y, 0.5f);
    EXPECT_FLOAT_EQ(bodies[0].halfExtents.z, 0.5f);
}

TEST(CollectRigidBodies, BoxWithOffset)
{
    entt::registry registry;
    auto entity = registry.create();
    registry.emplace<TransformComponent>(entity, glm::vec3(0.0f));
    auto& bc = registry.emplace<BoxColliderComponent>(entity);
    bc.Offset = glm::vec3(0.5f, 0.0f, 0.0f);

    auto bodies = CollectRigidBodies(&registry, 64, RigidBodyUploadFilter::AllColliders);
    ASSERT_EQ(bodies.size(), 1u);
    // 位置 = (0,0,0) + identity * ((0.5,0,0) * (1,1,1)) = (0.5, 0, 0)
    EXPECT_FLOAT_EQ(bodies[0].posAndType.x, 0.5f);
    EXPECT_FLOAT_EQ(bodies[0].posAndType.y, 0.0f);
    EXPECT_FLOAT_EQ(bodies[0].posAndType.z, 0.0f);
}

TEST(CollectRigidBodies, BoxWithScale)
{
    entt::registry registry;
    auto entity = registry.create();
    auto& tc = registry.emplace<TransformComponent>(entity, glm::vec3(0.0f));
    tc.Scale = glm::vec3(2.0f, 2.0f, 2.0f);
    registry.emplace<BoxColliderComponent>(entity);

    auto bodies = CollectRigidBodies(&registry, 64, RigidBodyUploadFilter::AllColliders);
    ASSERT_EQ(bodies.size(), 1u);
    // HalfExtents 应乘以 abs(Scale)
    EXPECT_FLOAT_EQ(bodies[0].halfExtents.x, 0.5f * 2.0f);
    EXPECT_FLOAT_EQ(bodies[0].halfExtents.y, 0.5f * 2.0f);
    EXPECT_FLOAT_EQ(bodies[0].halfExtents.z, 0.5f * 2.0f);
}

TEST(CollectRigidBodies, BoxWithRigidBodyComponent)
{
    entt::registry registry;
    auto entity = registry.create();
    registry.emplace<TransformComponent>(entity, glm::vec3(0.0f));
    registry.emplace<BoxColliderComponent>(entity);
    auto& rb = registry.emplace<RigidBodyComponent>(entity);
    rb.LinearVelocity  = glm::vec3(1.0f, 2.0f, 3.0f);
    rb.AngularVelocity = glm::vec3(0.1f, 0.2f, 0.3f);

    auto bodies = CollectRigidBodies(&registry, 64, RigidBodyUploadFilter::AllColliders);
    ASSERT_EQ(bodies.size(), 1u);
    EXPECT_FLOAT_EQ(bodies[0].linearVel.x, 1.0f);
    EXPECT_FLOAT_EQ(bodies[0].linearVel.y, 2.0f);
    EXPECT_FLOAT_EQ(bodies[0].linearVel.z, 3.0f);
    EXPECT_FLOAT_EQ(bodies[0].angularVel.x, 0.1f);
    EXPECT_FLOAT_EQ(bodies[0].angularVel.y, 0.2f);
    EXPECT_FLOAT_EQ(bodies[0].angularVel.z, 0.3f);
}

// ---- Sphere collider ----

TEST(CollectRigidBodies, SingleSphereCollider)
{
    entt::registry registry;
    auto entity = registry.create();
    registry.emplace<TransformComponent>(entity, glm::vec3(5.0f, 0.0f, 0.0f));
    registry.emplace<SphereColliderComponent>(entity);

    auto bodies = CollectRigidBodies(&registry, 64, RigidBodyUploadFilter::AllColliders);
    ASSERT_EQ(bodies.size(), 1u);
    // posAndType.w = 1 for sphere
    EXPECT_FLOAT_EQ(bodies[0].posAndType.w, 1.0f);
    // 默认 Radius = 0.5, Scale = (1,1,1) → halfExtents.x = 0.5
    EXPECT_FLOAT_EQ(bodies[0].halfExtents.x, 0.5f);
}

TEST(CollectRigidBodies, SphereWithNonUniformScale)
{
    entt::registry registry;
    auto entity = registry.create();
    auto& tc = registry.emplace<TransformComponent>(entity, glm::vec3(0.0f));
    tc.Scale = glm::vec3(1.0f, 3.0f, 1.0f); // max = 3
    auto& sc = registry.emplace<SphereColliderComponent>(entity);
    sc.Radius = 0.5f;

    auto bodies = CollectRigidBodies(&registry, 64, RigidBodyUploadFilter::AllColliders);
    ASSERT_EQ(bodies.size(), 1u);
    // sphere radius 应乘以 max(|Scale|) = 3
    EXPECT_FLOAT_EQ(bodies[0].halfExtents.x, 0.5f * 3.0f);
}

// ---- Filter 测试 ----

TEST(CollectRigidBodies, FilterRequireRigidBody_ExcludesWithout)
{
    entt::registry registry;
    auto withRB = registry.create();
    registry.emplace<TransformComponent>(withRB, glm::vec3(0.0f));
    registry.emplace<BoxColliderComponent>(withRB);
    registry.emplace<RigidBodyComponent>(withRB);

    auto withoutRB = registry.create();
    registry.emplace<TransformComponent>(withoutRB, glm::vec3(1.0f, 0.0f, 0.0f));
    registry.emplace<BoxColliderComponent>(withoutRB);

    auto bodies = CollectRigidBodies(&registry, 64, RigidBodyUploadFilter::RequireRigidBodyComponent);
    ASSERT_EQ(bodies.size(), 1u);
    // 只有带 RigidBodyComponent 的实体应被收集
    EXPECT_FLOAT_EQ(bodies[0].posAndType.x, 0.0f);
}

TEST(CollectRigidBodies, FilterAllColliders_IncludesAll)
{
    entt::registry registry;
    auto withRB = registry.create();
    registry.emplace<TransformComponent>(withRB, glm::vec3(0.0f));
    registry.emplace<BoxColliderComponent>(withRB);
    registry.emplace<RigidBodyComponent>(withRB);

    auto withoutRB = registry.create();
    registry.emplace<TransformComponent>(withoutRB, glm::vec3(1.0f, 0.0f, 0.0f));
    registry.emplace<BoxColliderComponent>(withoutRB);

    auto bodies = CollectRigidBodies(&registry, 64, RigidBodyUploadFilter::AllColliders);
    ASSERT_EQ(bodies.size(), 2u);
}

// ---- Max rigid body 截断 ----

TEST(CollectRigidBodies, MaxRigidBodiesTruncation)
{
    entt::registry registry;
    for (int i = 0; i < 10; i++)
    {
        auto entity = registry.create();
        registry.emplace<TransformComponent>(entity, glm::vec3(static_cast<float>(i), 0.0f, 0.0f));
        registry.emplace<BoxColliderComponent>(entity);
    }

    auto bodies = CollectRigidBodies(&registry, 3, RigidBodyUploadFilter::AllColliders);
    ASSERT_EQ(bodies.size(), 3u);
    // EnTT view 迭代顺序不保证，只验证收集了 3 个实体且 x 取值覆盖 [0,10)
    for (const auto& b : bodies)
    {
        EXPECT_GE(b.posAndType.x, 0.0f);
        EXPECT_LT(b.posAndType.x, 10.0f);
    }
    // 3 个应为不同的实体
    EXPECT_NE(bodies[0].posAndType.x, bodies[1].posAndType.x);
    EXPECT_NE(bodies[0].posAndType.x, bodies[2].posAndType.x);
}

// ---- 混合 Box + Sphere ----

TEST(CollectRigidBodies, MixedBoxAndSphere)
{
    entt::registry registry;

    auto box = registry.create();
    registry.emplace<TransformComponent>(box, glm::vec3(0.0f, 0.0f, 0.0f));
    registry.emplace<BoxColliderComponent>(box);

    auto sphere = registry.create();
    registry.emplace<TransformComponent>(sphere, glm::vec3(0.0f, 10.0f, 0.0f));
    registry.emplace<SphereColliderComponent>(sphere);

    auto bodies = CollectRigidBodies(&registry, 64, RigidBodyUploadFilter::AllColliders);
    ASSERT_EQ(bodies.size(), 2u);

    // Box 先收集（类型 0）
    EXPECT_FLOAT_EQ(bodies[0].posAndType.w, 0.0f);
    // Sphere 后收集（类型 1）
    EXPECT_FLOAT_EQ(bodies[1].posAndType.w, 1.0f);
}

// ---- Rotation 变换 ----

TEST(CollectRigidBodies, BoxWithRotation)
{
    entt::registry registry;
    auto entity = registry.create();
    auto& tc = registry.emplace<TransformComponent>(entity, glm::vec3(0.0f));
    tc.Rotation = glm::vec3(0.0f, glm::radians(90.0f), 0.0f); // Y 轴 90 度
    auto& bc = registry.emplace<BoxColliderComponent>(entity);
    bc.Offset = glm::vec3(1.0f, 0.0f, 0.0f);

    auto bodies = CollectRigidBodies(&registry, 64, RigidBodyUploadFilter::AllColliders);
    ASSERT_EQ(bodies.size(), 1u);

    // Y 轴旋转 90 度: (1,0,0) → (0,0,-1)
    // 位置 = (0,0,0) + rot_mat * (1,0,0) ≈ (0,0,-1)
    EXPECT_NEAR(bodies[0].posAndType.x, 0.0f, 1e-5f);
    EXPECT_NEAR(bodies[0].posAndType.y, 0.0f, 1e-5f);
    EXPECT_NEAR(bodies[0].posAndType.z, -1.0f, 1e-5f);

    // 旋转矩阵的列向量应反映 Y 轴 90 度旋转
    // rotCol1 = up vector → 应接近 (0,1,0)
    EXPECT_NEAR(bodies[0].rotCol1.x, 0.0f, 1e-5f);
    EXPECT_NEAR(bodies[0].rotCol1.y, 1.0f, 1e-5f);
    EXPECT_NEAR(bodies[0].rotCol1.z, 0.0f, 1e-5f);
}

// ---- 负 Scale ----

TEST(CollectRigidBodies, NegativeScale_TreatedAsAbsolute)
{
    entt::registry registry;
    auto entity = registry.create();
    auto& tc = registry.emplace<TransformComponent>(entity, glm::vec3(0.0f));
    tc.Scale = glm::vec3(-2.0f, -2.0f, -2.0f);
    registry.emplace<BoxColliderComponent>(entity);

    auto bodies = CollectRigidBodies(&registry, 64, RigidBodyUploadFilter::AllColliders);
    ASSERT_EQ(bodies.size(), 1u);
    // halfExtents 应使用 abs(Scale)
    EXPECT_FLOAT_EQ(bodies[0].halfExtents.x, 0.5f * 2.0f);
    EXPECT_FLOAT_EQ(bodies[0].halfExtents.y, 0.5f * 2.0f);
    EXPECT_FLOAT_EQ(bodies[0].halfExtents.z, 0.5f * 2.0f);
}

// ---- 同时有 Box 和 Sphere 的实体（不实际，但验证不会被重复收集） ----

TEST(CollectRigidBodies, EntityWithBothBoxAndSphere)
{
    entt::registry registry;
    auto entity = registry.create();
    registry.emplace<TransformComponent>(entity, glm::vec3(0.0f));
    registry.emplace<BoxColliderComponent>(entity);
    registry.emplace<SphereColliderComponent>(entity);

    auto bodies = CollectRigidBodies(&registry, 64, RigidBodyUploadFilter::AllColliders);
    // 同实体同时有 Box 和 Sphere → 会被 Box view 和 Sphere view 各收集一次
    ASSERT_EQ(bodies.size(), 2u);
    EXPECT_FLOAT_EQ(bodies[0].posAndType.w, 0.0f); // box
    EXPECT_FLOAT_EQ(bodies[1].posAndType.w, 1.0f); // sphere
}
