#include <gtest/gtest.h>
#include "Asset/SlotMap.h"
#include <string>
#include <memory>

using namespace Engine;

class DummyResource
{
public:
    int value;
    DummyResource(int v = 0) : value(v) {}
};

TEST(SlotMap, Initialization)
{
    SlotMap<DummyResource> map;
    EXPECT_EQ(map.Size(), 0);
}

TEST(SlotMap, InsertAndGet)
{
    SlotMap<DummyResource> map;
    auto res = std::make_shared<DummyResource>(42);
    AssetHandle handle = map.Insert(res, "path/to/res");

    EXPECT_TRUE(handle.IsValid());
    EXPECT_EQ(map.Size(), 1);

    DummyResource* retrieved = map.Get(handle);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->value, 42);

    auto retrievedRef = map.GetRef(handle);
    ASSERT_NE(retrievedRef, nullptr);
    EXPECT_EQ(retrievedRef->value, 42);

    EXPECT_EQ(map.GetPath(handle), "path/to/res");
}

TEST(SlotMap, Replace)
{
    SlotMap<DummyResource> map;
    AssetHandle handle = map.Insert(std::make_shared<DummyResource>(10), "path");

    map.Replace(handle, std::make_shared<DummyResource>(20));

    DummyResource* retrieved = map.Get(handle);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->value, 20);
}

TEST(SlotMap, RemoveAndGenerationInvalidation)
{
    SlotMap<DummyResource> map;
    AssetHandle handle = map.Insert(std::make_shared<DummyResource>(100), "path1");

    EXPECT_EQ(map.Size(), 1);

    map.Remove(handle);

    // Size() now tracks active elements, so Remove decrements it
    EXPECT_EQ(map.Size(), 0);

    DummyResource* afterRemove = map.Get(handle);
    EXPECT_EQ(afterRemove, nullptr);

    auto afterRemoveRef = map.GetRef(handle);
    EXPECT_EQ(afterRemoveRef, nullptr);

    EXPECT_EQ(map.GetPath(handle), "");

    // Now insert a new item. It should reuse the slot.
    AssetHandle handle2 = map.Insert(std::make_shared<DummyResource>(200), "path2");

    // They should share the same index but different generation
    EXPECT_EQ(handle.Index, handle2.Index);
    EXPECT_NE(handle.Generation, handle2.Generation);

    // Old handle is still invalid
    EXPECT_EQ(map.Get(handle), nullptr);

    // New handle works
    ASSERT_NE(map.Get(handle2), nullptr);
    EXPECT_EQ(map.Get(handle2)->value, 200);
}

TEST(SlotMap, GetInvalidHandle)
{
    SlotMap<DummyResource> map;
    AssetHandle handle{999, 1}; // Out of bounds

    EXPECT_EQ(map.Get(handle), nullptr);
    EXPECT_EQ(map.GetRef(handle), nullptr);
    EXPECT_EQ(map.GetPath(handle), "");
}

TEST(SlotMap, Clear)
{
    SlotMap<DummyResource> map;
    AssetHandle h1 = map.Insert(std::make_shared<DummyResource>(1), "p1");
    AssetHandle h2 = map.Insert(std::make_shared<DummyResource>(2), "p2");

    EXPECT_EQ(map.Size(), 2);

    map.Clear();

    EXPECT_EQ(map.Size(), 0);
    EXPECT_EQ(map.Get(h1), nullptr);
    EXPECT_EQ(map.Get(h2), nullptr);

    AssetHandle h3 = map.Insert(std::make_shared<DummyResource>(3), "p3");
    EXPECT_TRUE(h3.IsValid());
    EXPECT_EQ(map.Size(), 1);
}
