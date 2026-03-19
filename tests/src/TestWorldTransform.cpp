#include <gtest/gtest.h>
#include "Scene/Components.h"
#include "Scene/SceneEntityIndex.h"
#include "Scene/WorldTransformService.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace Engine;

class WorldTransformTest : public ::testing::Test
{
protected:
    entt::registry   reg;
    SceneEntityIndex index;

    entt::entity CreateEntity(uint64_t uuid, glm::vec3 translation = {0, 0, 0}, glm::vec3 scale = {1, 1, 1})
    {
        auto entity = reg.create();
        reg.emplace<IDComponent>(entity, UUID(uuid));
        auto& tc       = reg.emplace<TransformComponent>(entity);
        tc.Translation = translation;
        tc.Scale       = scale;
        reg.emplace<RelationshipComponent>(entity);
        index.Insert(UUID(uuid), entity);
        return entity;
    }

    entt::entity CreateEntity(uint64_t uuid, glm::vec3 translation, glm::vec3 rotation, glm::vec3 scale)
    {
        auto entity = reg.create();
        reg.emplace<IDComponent>(entity, UUID(uuid));
        auto& tc       = reg.emplace<TransformComponent>(entity);
        tc.Translation = translation;
        tc.Rotation    = rotation;
        tc.Scale       = scale;
        reg.emplace<RelationshipComponent>(entity);
        index.Insert(UUID(uuid), entity);
        return entity;
    }

    void ExpectMatNear(const glm::mat4& a, const glm::mat4& b, float eps = 1e-3f)
    {
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                EXPECT_NEAR(a[c][r], b[c][r], eps) << "Mismatch at [" << c << "][" << r << "]";
    }

    void SetParent(entt::entity child, entt::entity parent)
    {
        auto& childRel    = reg.get<RelationshipComponent>(child);
        auto& parentRel   = reg.get<RelationshipComponent>(parent);
        childRel.ParentID = reg.get<IDComponent>(parent).ID;
        parentRel.Children.push_back(reg.get<IDComponent>(child).ID);
    }
};

TEST_F(WorldTransformTest, RootEntityReturnsLocalTransform)
{
    auto entity = CreateEntity(1, {5.0f, 0.0f, 0.0f});

    glm::mat4 world = WorldTransformService::ComputeWorldTransform(reg, entity, index);
    // Translation 应在第 4 列
    EXPECT_FLOAT_EQ(world[3][0], 5.0f);
    EXPECT_FLOAT_EQ(world[3][1], 0.0f);
    EXPECT_FLOAT_EQ(world[3][2], 0.0f);
}

TEST_F(WorldTransformTest, ChildCombinesWithParent)
{
    auto parent = CreateEntity(1, {10.0f, 0.0f, 0.0f});
    auto child  = CreateEntity(2, {5.0f, 0.0f, 0.0f});

    SetParent(child, parent);

    glm::mat4 world = WorldTransformService::ComputeWorldTransform(reg, child, index);
    // 子在父空间偏移 5，父在世界偏移 10 → 世界 x = 15
    EXPECT_FLOAT_EQ(world[3][0], 15.0f);
}

TEST_F(WorldTransformTest, DeepHierarchy)
{
    auto e1 = CreateEntity(1, {1.0f, 0.0f, 0.0f});
    auto e2 = CreateEntity(2, {2.0f, 0.0f, 0.0f});
    auto e3 = CreateEntity(3, {3.0f, 0.0f, 0.0f});

    SetParent(e2, e1);
    SetParent(e3, e2);

    glm::mat4 world = WorldTransformService::ComputeWorldTransform(reg, e3, index);
    // 1 + 2 + 3 = 6
    EXPECT_FLOAT_EQ(world[3][0], 6.0f);
}

TEST_F(WorldTransformTest, ScalePropagation)
{
    auto parent = CreateEntity(1, {0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f});
    auto child  = CreateEntity(2, {5.0f, 0.0f, 0.0f});

    SetParent(child, parent);

    glm::mat4 world = WorldTransformService::ComputeWorldTransform(reg, child, index);
    // 父缩放 2x，子在本地 x=5 → 世界 x = 5*2 = 10
    EXPECT_FLOAT_EQ(world[3][0], 10.0f);
}

TEST_F(WorldTransformTest, IdentityTransform)
{
    auto entity = CreateEntity(1);

    glm::mat4 world    = WorldTransformService::ComputeWorldTransform(reg, entity, index);
    glm::mat4 identity = glm::mat4(1.0f);

    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            EXPECT_NEAR(world[c][r], identity[c][r], 1e-5f);
}

// ── 旋转与非均匀缩放测试 ──────────────────────────────

TEST_F(WorldTransformTest, RotatedParentWithTranslatedChild)
{
    const float halfPi = glm::half_pi<float>();
    // 父绕 Z 轴旋转 90°，子在本地空间 (1,0,0)
    auto parent = CreateEntity(1, {0, 0, 0}, {0, 0, halfPi}, {1, 1, 1});
    auto child  = CreateEntity(2, {1, 0, 0});

    SetParent(child, parent);

    glm::mat4 world = WorldTransformService::ComputeWorldTransform(reg, child, index);
    // 父旋转 90° 将子的本地 X 方向映射到世界 Y 方向
    EXPECT_NEAR(world[3][0], 0.0f, 1e-4f);
    EXPECT_NEAR(world[3][1], 1.0f, 1e-4f);
    EXPECT_NEAR(world[3][2], 0.0f, 1e-4f);
}

TEST_F(WorldTransformTest, NonUniformScaleParent)
{
    // 父 Scale=(2,3,1)，子在本地空间 (1,1,0)
    auto parent = CreateEntity(1, {0, 0, 0}, {2, 3, 1});
    auto child  = CreateEntity(2, {1, 1, 0});

    SetParent(child, parent);

    glm::mat4 world = WorldTransformService::ComputeWorldTransform(reg, child, index);
    // 世界坐标 = (1*2, 1*3, 0)
    EXPECT_NEAR(world[3][0], 2.0f, 1e-4f);
    EXPECT_NEAR(world[3][1], 3.0f, 1e-4f);
    EXPECT_NEAR(world[3][2], 0.0f, 1e-4f);
}

TEST_F(WorldTransformTest, RotationAndNonUniformScaleCombined)
{
    const float halfPi = glm::half_pi<float>();
    // 父绕 Z 轴旋转 90° + Scale=(2,3,1)，子在本地空间 (1,0,0)
    // GetTransform() = T * R * S，子本地 (1,0,0) 经父 S 缩放 → (2,0,0)，再经父 R 旋转 → (0,2,0)
    auto parent = CreateEntity(1, {0, 0, 0}, {0, 0, halfPi}, {2, 3, 1});
    auto child  = CreateEntity(2, {1, 0, 0});

    SetParent(child, parent);

    glm::mat4 world = WorldTransformService::ComputeWorldTransform(reg, child, index);
    EXPECT_NEAR(world[3][0], 0.0f, 1e-4f);
    EXPECT_NEAR(world[3][1], 2.0f, 1e-4f);
    EXPECT_NEAR(world[3][2], 0.0f, 1e-4f);
}

TEST_F(WorldTransformTest, DeepRotationPropagation)
{
    const float halfPi = glm::half_pi<float>();
    // 3 层实体各绕 Z 轴旋转 90°，累计 180°
    auto e1 = CreateEntity(1, {0, 0, 0}, {0, 0, halfPi}, {1, 1, 1});
    auto e2 = CreateEntity(2, {0, 0, 0}, {0, 0, halfPi}, {1, 1, 1});
    auto e3 = CreateEntity(3, {1, 0, 0});

    SetParent(e2, e1);
    SetParent(e3, e2);

    glm::mat4 world = WorldTransformService::ComputeWorldTransform(reg, e3, index);
    // 累计旋转 180°，本地 (1,0,0) → 世界 (-1,0,0)
    EXPECT_NEAR(world[3][0], -1.0f, 1e-4f);
    EXPECT_NEAR(world[3][1], 0.0f, 1e-4f);
    EXPECT_NEAR(world[3][2], 0.0f, 1e-4f);
}
