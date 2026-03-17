#include <gtest/gtest.h>
#include "Scene/SceneEntityIndex.h"

using Engine::SceneEntityIndex;
using Engine::UUID;

TEST(SceneEntityIndex, InsertAndFind)
{
    SceneEntityIndex index;
    UUID             uuid(100);
    auto             entity = static_cast<entt::entity>(1);

    index.Insert(uuid, entity);
    EXPECT_EQ(index.Find(uuid), entity);
}

TEST(SceneEntityIndex, FindNonExistent)
{
    SceneEntityIndex index;
    UUID             uuid(999);
    EXPECT_TRUE(index.Find(uuid) == entt::null);
}

TEST(SceneEntityIndex, Remove)
{
    SceneEntityIndex index;
    UUID             uuid(100);
    auto             entity = static_cast<entt::entity>(1);

    index.Insert(uuid, entity);
    index.Remove(uuid);
    EXPECT_TRUE(index.Find(uuid) == entt::null);
}

TEST(SceneEntityIndex, RemoveNonExistent)
{
    SceneEntityIndex index;
    UUID             uuid(999);
    // 不应崩溃
    index.Remove(uuid);
    EXPECT_EQ(index.Size(), 0u);
}

TEST(SceneEntityIndex, Clear)
{
    SceneEntityIndex index;
    index.Insert(UUID(1), static_cast<entt::entity>(1));
    index.Insert(UUID(2), static_cast<entt::entity>(2));
    index.Insert(UUID(3), static_cast<entt::entity>(3));

    index.Clear();
    EXPECT_EQ(index.Size(), 0u);
    EXPECT_TRUE(index.Find(UUID(1)) == entt::null);
}

TEST(SceneEntityIndex, Size)
{
    SceneEntityIndex index;
    EXPECT_EQ(index.Size(), 0u);

    index.Insert(UUID(1), static_cast<entt::entity>(1));
    EXPECT_EQ(index.Size(), 1u);

    index.Insert(UUID(2), static_cast<entt::entity>(2));
    EXPECT_EQ(index.Size(), 2u);
}

TEST(SceneEntityIndex, OverwriteExisting)
{
    SceneEntityIndex index;
    UUID             uuid(100);
    auto             entity1 = static_cast<entt::entity>(1);
    auto             entity2 = static_cast<entt::entity>(2);

    index.Insert(uuid, entity1);
    index.Insert(uuid, entity2);

    EXPECT_EQ(index.Find(uuid), entity2);
    EXPECT_EQ(index.Size(), 1u);
}

TEST(SceneEntityIndex, MultipleEntries)
{
    SceneEntityIndex index;
    UUID             uuid1(10), uuid2(20), uuid3(30);
    auto             e1 = static_cast<entt::entity>(1);
    auto             e2 = static_cast<entt::entity>(2);
    auto             e3 = static_cast<entt::entity>(3);

    index.Insert(uuid1, e1);
    index.Insert(uuid2, e2);
    index.Insert(uuid3, e3);

    EXPECT_EQ(index.Find(uuid1), e1);
    EXPECT_EQ(index.Find(uuid2), e2);
    EXPECT_EQ(index.Find(uuid3), e3);
    EXPECT_EQ(index.Size(), 3u);
}
