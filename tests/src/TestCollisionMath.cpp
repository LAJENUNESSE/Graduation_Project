#include <gtest/gtest.h>
#include "Physics/CollisionMath.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

using namespace Engine;

TEST(CollisionMath, SphereSphereOverlap)
{
    CollisionInfo info;
    bool          result = CollisionMath::SphereSphere({0, 0, 0}, 1.0f, {1.5f, 0, 0}, 1.0f, info);
    EXPECT_TRUE(result);
    EXPECT_NEAR(info.penetrationDepth, 0.5f, 1e-5f);
    EXPECT_NEAR(info.contactNormal.x, 1.0f, 1e-5f);
}

TEST(CollisionMath, SphereSphereNoOverlap)
{
    CollisionInfo info;
    bool          result = CollisionMath::SphereSphere({0, 0, 0}, 1.0f, {3.0f, 0, 0}, 1.0f, info);
    EXPECT_FALSE(result);
}

TEST(CollisionMath, AABBAABBOverlap)
{
    CollisionInfo info;
    bool          result = CollisionMath::AABBAABB({0, 0, 0}, {1, 1, 1}, {1.5f, 0, 0}, {1, 1, 1}, info);
    EXPECT_TRUE(result);
    EXPECT_NEAR(info.penetrationDepth, 0.5f, 1e-5f);
}

TEST(CollisionMath, AABBAABBNoOverlap)
{
    CollisionInfo info;
    bool          result = CollisionMath::AABBAABB({0, 0, 0}, {1, 1, 1}, {3.0f, 0, 0}, {1, 1, 1}, info);
    EXPECT_FALSE(result);
}

TEST(CollisionMath, OBBOBBAligned)
{
    CollisionInfo info;
    glm::quat    identity = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    bool result = CollisionMath::OBBOBB({0, 0, 0}, {1, 1, 1}, identity, {1.5f, 0, 0}, {1, 1, 1}, identity, info);
    EXPECT_TRUE(result);
    EXPECT_NEAR(info.penetrationDepth, 0.5f, 1e-5f);
}

TEST(CollisionMath, OBBOBBRotated45)
{
    CollisionInfo info;
    glm::quat    identity  = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    // 绕 Z 轴旋转 45 度
    glm::quat    rotated45 = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 0, 1));

    // 两个单位 OBB，间距 2.0（对齐时刚好不碰，旋转后应该碰）
    // 对角线 sqrt(2) ≈ 1.414，旋转后 halfExtent 投影变大
    bool result = CollisionMath::OBBOBB({0, 0, 0}, {1, 1, 1}, identity, {2.0f, 0, 0}, {1, 1, 1}, rotated45, info);
    EXPECT_TRUE(result);
    EXPECT_GT(info.penetrationDepth, 0.0f);
}
