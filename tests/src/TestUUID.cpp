#include <gtest/gtest.h>
#include "Core/UUID.h"

#include <set>
#include <thread>
#include <vector>

using Engine::UUID;

TEST(UUID, DefaultConstructorGeneratesNonZero)
{
    UUID uuid;
    EXPECT_NE(static_cast<uint64_t>(uuid), 0u);
}

TEST(UUID, TwoUUIDsAreDifferent)
{
    UUID a, b;
    EXPECT_NE(static_cast<uint64_t>(a), static_cast<uint64_t>(b));
}

TEST(UUID, ConstructFromUint64)
{
    UUID uuid(12345u);
    EXPECT_EQ(static_cast<uint64_t>(uuid), 12345u);
}

TEST(UUID, ConversionToUint64)
{
    UUID     uuid(99999u);
    uint64_t value = uuid;
    EXPECT_EQ(value, 99999u);
}

TEST(UUID, CopyConstructor)
{
    UUID original(42u);
    UUID copy(original);
    EXPECT_EQ(static_cast<uint64_t>(copy), 42u);
}

TEST(UUID, HashConsistency)
{
    UUID   uuid(100u);
    size_t h1 = std::hash<UUID>{}(uuid);
    size_t h2 = std::hash<UUID>{}(uuid);
    EXPECT_EQ(h1, h2);
}

TEST(UUID, HashDifference)
{
    UUID   a(100u), b(200u);
    size_t h1 = std::hash<UUID>{}(a);
    size_t h2 = std::hash<UUID>{}(b);
    EXPECT_NE(h1, h2);
}

TEST(UUID, ThreadSafety)
{
    constexpr int             kThreads = 8;
    constexpr int             kPerThread = 1000;
    std::vector<std::thread>  threads;
    std::vector<uint64_t>     results(kThreads * kPerThread, 0);

    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&results, t]() {
            for (int i = 0; i < kPerThread; ++i)
            {
                UUID uuid;
                results[t * kPerThread + i] = static_cast<uint64_t>(uuid);
            }
        });
    }

    for (auto& th : threads)
        th.join();

    // 所有 UUID 应唯一（概率极低重复）
    std::set<uint64_t> unique(results.begin(), results.end());
    EXPECT_EQ(unique.size(), results.size());
}
