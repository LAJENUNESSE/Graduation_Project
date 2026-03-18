#include <gtest/gtest.h>
#include "Physics/SDFMath.h"

#include <cmath>

using namespace Engine::SDFMath;

// --- Box SDF ---

TEST(SDFMath, BoxSDFOutside)
{
    // 点在盒子外面（x 方向）
    auto r = BoxSDF(2.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    EXPECT_GT(r.sdf, 0.0f);
    EXPECT_NEAR(r.sdf, 1.0f, 1e-5f);
}

TEST(SDFMath, BoxSDFInside)
{
    // 点在盒子中心
    auto r = BoxSDF(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    EXPECT_LT(r.sdf, 0.0f);
    EXPECT_NEAR(r.sdf, -1.0f, 1e-5f);
}

TEST(SDFMath, BoxSDFOnSurface)
{
    // 点在盒子表面
    auto r = BoxSDF(1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    EXPECT_NEAR(r.sdf, 0.0f, 1e-5f);
}

TEST(SDFMath, BoxSDFNormalDirection)
{
    // 点在 +x 方向外面，法线应指向 +x
    auto r = BoxSDF(3.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    EXPECT_FLOAT_EQ(r.normalX, 1.0f);
    EXPECT_FLOAT_EQ(r.normalY, 0.0f);
    EXPECT_FLOAT_EQ(r.normalZ, 0.0f);
}

// --- Sphere SDF ---

TEST(SDFMath, SphereSDFOutside)
{
    auto r = SphereSDF(2.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_GT(r.sdf, 0.0f);
    EXPECT_NEAR(r.sdf, 1.0f, 1e-5f);
}

TEST(SDFMath, SphereSDFInside)
{
    auto r = SphereSDF(0.5f, 0.0f, 0.0f, 1.0f);
    EXPECT_LT(r.sdf, 0.0f);
    EXPECT_NEAR(r.sdf, -0.5f, 1e-5f);
}

TEST(SDFMath, SphereSDFCenter)
{
    // 点在球心 → 法线默认指向 +y
    auto r = SphereSDF(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(r.sdf, -1.0f, 1e-5f);
    EXPECT_FLOAT_EQ(r.normalX, 0.0f);
    EXPECT_FLOAT_EQ(r.normalY, 1.0f);
    EXPECT_FLOAT_EQ(r.normalZ, 0.0f);
}
