#include <gtest/gtest.h>
#include "Asset/AssetHandle.h"
#include <unordered_set>

using namespace Engine;

TEST(AssetHandle, DefaultConstructorIsInvalid)
{
    AssetHandle handle;
    EXPECT_EQ(handle.Index, 0);
    EXPECT_EQ(handle.Generation, 0);
    EXPECT_FALSE(handle.IsValid());
    EXPECT_FALSE(static_cast<bool>(handle));
}

TEST(AssetHandle, ValidHandle)
{
    AssetHandle handle{1, 1};
    EXPECT_TRUE(handle.IsValid());
    EXPECT_TRUE(static_cast<bool>(handle));

    AssetHandle handle2{0, 1}; // valid if generation > 0
    EXPECT_TRUE(handle2.IsValid());

    AssetHandle handle3{1, 0}; // valid if index > 0
    EXPECT_TRUE(handle3.IsValid());
}

TEST(AssetHandle, Equality)
{
    AssetHandle h1{1, 1};
    AssetHandle h2{1, 1};
    AssetHandle h3{1, 2};
    AssetHandle h4{2, 1};

    EXPECT_TRUE(h1 == h2);
    EXPECT_FALSE(h1 == h3);
    EXPECT_FALSE(h1 == h4);
}

TEST(AssetHandle, Inequality)
{
    AssetHandle h1{1, 1};
    AssetHandle h2{1, 1};
    AssetHandle h3{1, 2};

    EXPECT_FALSE(h1 != h2);
    EXPECT_TRUE(h1 != h3);
}

TEST(AssetHandle, HashFunction)
{
    AssetHandle h1{1, 1};
    AssetHandle h2{1, 1};
    AssetHandle h3{1, 2};

    std::hash<AssetHandle> hasher;

    // Equal handles must have equal hashes
    EXPECT_EQ(hasher(h1), hasher(h2));

    // Different handles should ideally have different hashes (though collisions are possible, these specific simple ones won't collide)
    EXPECT_NE(hasher(h1), hasher(h3));

    // Can be used in unordered_set
    std::unordered_set<AssetHandle> handleSet;
    handleSet.insert(h1);
    EXPECT_TRUE(handleSet.find(h2) != handleSet.end());
    EXPECT_TRUE(handleSet.find(h3) == handleSet.end());
}
