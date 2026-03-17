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

    void SetParent(entt::entity child, entt::entity parent)
    {
        auto& childRel  = reg.get<RelationshipComponent>(child);
        auto& parentRel = reg.get<RelationshipComponent>(parent);
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
