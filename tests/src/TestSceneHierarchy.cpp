#include <gtest/gtest.h>
#include "Scene/Components.h"
#include "Scene/SceneEntityIndex.h"
#include "Scene/SceneHierarchyService.h"
#include "Scene/WorldTransformService.h"

#include <glm/glm.hpp>

using namespace Engine;

// 测试 fixture：构造 registry + index，手动创建实体
class SceneHierarchyTest : public ::testing::Test
{
protected:
    entt::registry   reg;
    SceneEntityIndex index;

    // 创建带 ID / Tag / Transform / Relationship 的实体
    entt::entity CreateEntity(uint64_t uuid)
    {
        auto entity = reg.create();
        reg.emplace<IDComponent>(entity, UUID(uuid));
        reg.emplace<TagComponent>(entity, "Entity_" + std::to_string(uuid));
        reg.emplace<TransformComponent>(entity);
        reg.emplace<RelationshipComponent>(entity);
        index.Insert(UUID(uuid), entity);
        return entity;
    }

    entt::entity
    CreateEntity(uint64_t uuid, glm::vec3 translation, glm::vec3 rotation = {0, 0, 0}, glm::vec3 scale = {1, 1, 1})
    {
        auto  entity   = CreateEntity(uuid);
        auto& tc       = reg.get<TransformComponent>(entity);
        tc.Translation = translation;
        tc.Rotation    = rotation;
        tc.Scale       = scale;
        return entity;
    }

    void ExpectMatNear(const glm::mat4& a, const glm::mat4& b, float eps = 1e-3f)
    {
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                EXPECT_NEAR(a[c][r], b[c][r], eps) << "Mismatch at [" << c << "][" << r << "]";
    }
};

TEST_F(SceneHierarchyTest, SetParentBasic)
{
    auto parent = CreateEntity(1);
    auto child  = CreateEntity(2);

    SceneHierarchyService::SetParent(reg, index, child, parent);

    auto& childRel = reg.get<RelationshipComponent>(child);
    EXPECT_EQ(static_cast<uint64_t>(childRel.ParentID), 1u);

    auto& parentRel = reg.get<RelationshipComponent>(parent);
    ASSERT_EQ(parentRel.Children.size(), 1u);
    EXPECT_EQ(static_cast<uint64_t>(parentRel.Children[0]), 2u);
}

TEST_F(SceneHierarchyTest, SetParentCycleDetection)
{
    auto parent = CreateEntity(1);
    auto child  = CreateEntity(2);

    SceneHierarchyService::SetParent(reg, index, child, parent);
    // 尝试让 parent 成为 child 的子节点 → 应被拒绝（循环）
    SceneHierarchyService::SetParent(reg, index, parent, child);

    auto& parentRel = reg.get<RelationshipComponent>(parent);
    EXPECT_EQ(static_cast<uint64_t>(parentRel.ParentID), 0u); // 仍是根节点
}

TEST_F(SceneHierarchyTest, SetParentSelfReference)
{
    auto entity = CreateEntity(1);

    SceneHierarchyService::SetParent(reg, index, entity, entity);

    auto& rel = reg.get<RelationshipComponent>(entity);
    EXPECT_EQ(static_cast<uint64_t>(rel.ParentID), 0u);
    EXPECT_TRUE(rel.Children.empty());
}

TEST_F(SceneHierarchyTest, RemoveParentBasic)
{
    auto parent = CreateEntity(1);
    auto child  = CreateEntity(2);

    SceneHierarchyService::SetParent(reg, index, child, parent);
    SceneHierarchyService::RemoveParent(reg, index, child);

    auto& childRel = reg.get<RelationshipComponent>(child);
    EXPECT_EQ(static_cast<uint64_t>(childRel.ParentID), 0u);

    auto& parentRel = reg.get<RelationshipComponent>(parent);
    EXPECT_TRUE(parentRel.Children.empty());
}

TEST_F(SceneHierarchyTest, GetChildren)
{
    auto parent = CreateEntity(1);
    auto child1 = CreateEntity(2);
    auto child2 = CreateEntity(3);

    SceneHierarchyService::SetParent(reg, index, child1, parent);
    SceneHierarchyService::SetParent(reg, index, child2, parent);

    auto children = SceneHierarchyService::GetChildren(reg, index, parent);
    EXPECT_EQ(children.size(), 2u);
}

TEST_F(SceneHierarchyTest, GetChildrenEmpty)
{
    auto entity   = CreateEntity(1);
    auto children = SceneHierarchyService::GetChildren(reg, index, entity);
    EXPECT_TRUE(children.empty());
}

TEST_F(SceneHierarchyTest, IsAncestorOfDirect)
{
    auto parent = CreateEntity(1);
    auto child  = CreateEntity(2);

    SceneHierarchyService::SetParent(reg, index, child, parent);
    EXPECT_TRUE(SceneHierarchyService::IsAncestorOf(reg, index, parent, child));
}

TEST_F(SceneHierarchyTest, IsAncestorOfTransitive)
{
    auto grandparent = CreateEntity(1);
    auto parent      = CreateEntity(2);
    auto child       = CreateEntity(3);

    SceneHierarchyService::SetParent(reg, index, parent, grandparent);
    SceneHierarchyService::SetParent(reg, index, child, parent);

    EXPECT_TRUE(SceneHierarchyService::IsAncestorOf(reg, index, grandparent, child));
}

TEST_F(SceneHierarchyTest, IsAncestorOfNotRelated)
{
    auto a = CreateEntity(1);
    auto b = CreateEntity(2);

    EXPECT_FALSE(SceneHierarchyService::IsAncestorOf(reg, index, a, b));
}

TEST_F(SceneHierarchyTest, GetRootEntities)
{
    auto root1 = CreateEntity(1);
    auto root2 = CreateEntity(2);
    auto child = CreateEntity(3);

    SceneHierarchyService::SetParent(reg, index, child, root1);

    auto roots = SceneHierarchyService::GetRootEntities(reg);
    EXPECT_EQ(roots.size(), 2u); // root1 和 root2
}

// ── 世界坐标连续性测试 ──────────────────────────────

TEST_F(SceneHierarchyTest, SetParentPreservesWorldTransform)
{
    const float pi4    = glm::quarter_pi<float>();
    auto        parent = CreateEntity(1, {10, 0, 0}, {0, 0, pi4});         // 平移 + 旋转 45°
    auto        child  = CreateEntity(2, {5, 3, 0}, {0, 0, 0}, {2, 2, 2}); // 平移 + 均匀缩放

    glm::mat4 worldBefore = WorldTransformService::ComputeWorldTransform(reg, child, index);

    SceneHierarchyService::SetParent(reg, index, child, parent);

    glm::mat4 worldAfter = WorldTransformService::ComputeWorldTransform(reg, child, index);

    // SetParent 应保持子实体的世界变换不变（decompose + 回写往返精度）
    ExpectMatNear(worldBefore, worldAfter);
}

TEST_F(SceneHierarchyTest, RemoveParentPreservesWorldTransform)
{
    const float pi3    = glm::pi<float>() / 3.0f;
    auto        parent = CreateEntity(1, {10, 0, 0}, {0, 0, pi3}); // 平移 + 旋转 60°
    auto        child  = CreateEntity(2, {3, 0, 0});

    SceneHierarchyService::SetParent(reg, index, child, parent);

    glm::mat4 worldBefore = WorldTransformService::ComputeWorldTransform(reg, child, index);

    SceneHierarchyService::RemoveParent(reg, index, child);

    glm::mat4 worldAfter = WorldTransformService::ComputeWorldTransform(reg, child, index);

    // RemoveParent 应保持子实体的世界变换不变
    ExpectMatNear(worldBefore, worldAfter);
}
