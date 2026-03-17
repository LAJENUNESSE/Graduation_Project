#include <gtest/gtest.h>
#include "Scene/Components.h"
#include "Scene/SceneEntityIndex.h"
#include "Scene/SceneHierarchyService.h"

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
    auto entity  = CreateEntity(1);
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
    auto child  = CreateEntity(3);

    SceneHierarchyService::SetParent(reg, index, child, root1);

    auto roots = SceneHierarchyService::GetRootEntities(reg);
    EXPECT_EQ(roots.size(), 2u); // root1 和 root2
}
