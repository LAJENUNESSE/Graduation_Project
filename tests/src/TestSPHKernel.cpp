#include <gtest/gtest.h>
#include "Renderer/SPHKernelMath.h"

#include <cmath>

using namespace Engine::SPHKernelMath;

static constexpr float PI = 3.14159265358979323846f;

TEST(SPHKernel, ComputeBasicRadius)
{
    auto p = Compute(0.1f);
    EXPECT_FLOAT_EQ(p.h, 0.1f);
    EXPECT_FLOAT_EQ(p.h2, 0.01f);
    EXPECT_GT(p.poly6Coeff, 0.0f);
    EXPECT_LT(p.spikyCoeff, 0.0f);
}

TEST(SPHKernel, ComputeRadiusOne)
{
    auto p = Compute(1.0f);
    EXPECT_FLOAT_EQ(p.h, 1.0f);
    EXPECT_FLOAT_EQ(p.h2, 1.0f);
    EXPECT_FLOAT_EQ(p.h6, 1.0f);
    EXPECT_FLOAT_EQ(p.h9, 1.0f);
}

TEST(SPHKernel, Poly6CoeffFormula)
{
    float h        = 0.5f;
    auto  p        = Compute(h);
    float h9       = std::pow(h, 9.0f);
    float expected = 315.0f / (64.0f * PI * h9);
    EXPECT_NEAR(p.poly6Coeff, expected, expected * 1e-5f);
}

TEST(SPHKernel, SpikyCoeffFormula)
{
    float h        = 0.5f;
    auto  p        = Compute(h);
    float h6       = std::pow(h, 6.0f);
    float expected = -45.0f / (PI * h6);
    EXPECT_NEAR(p.spikyCoeff, expected, std::abs(expected) * 1e-5f);
}

TEST(SPHKernel, DerivedPowersConsistent)
{
    auto p = Compute(0.3f);
    EXPECT_NEAR(p.h2, p.h * p.h, 1e-7f);
    EXPECT_NEAR(p.h6, p.h2 * p.h2 * p.h2, 1e-7f);
    EXPECT_NEAR(p.h9, p.h6 * p.h2 * p.h, 1e-7f);
}

TEST(SPHKernel, SmallRadiusStability)
{
    // 非常小的半径不应产生 NaN 或 Inf
    auto p = Compute(0.001f);
    EXPECT_FALSE(std::isnan(p.poly6Coeff));
    EXPECT_FALSE(std::isinf(p.poly6Coeff));
    EXPECT_FALSE(std::isnan(p.spikyCoeff));
    EXPECT_FALSE(std::isinf(p.spikyCoeff));
}

// ---- PCISPH δ 自动推导测试 ----

TEST(SPHKernel, PCISPHDelta_StandardParams)
{
    // 标准参数下 δ 应为负值，量级合理
    float delta = ComputePCISPHDelta(0.1f, 0.02f, 1000.0f, 0.016f);
    EXPECT_LT(delta, 0.0f) << "delta should be negative";
    EXPECT_GT(delta, -1e6f) << "delta magnitude should be reasonable";
    EXPECT_LT(delta, -1e-6f) << "delta should not be near zero";
}

TEST(SPHKernel, PCISPHDelta_VerySmallDt_Fallback)
{
    // dt 极小时 beta 趋向 0，应返回 fallback 值 0.3
    float delta = ComputePCISPHDelta(0.1f, 0.02f, 1000.0f, 1e-10f);
    EXPECT_FLOAT_EQ(delta, 0.3f) << "should return fallback when beta ≈ 0";
}

TEST(SPHKernel, PCISPHDelta_NotNanOrInf)
{
    // 各种参数组合不应产生 NaN 或 Inf
    float params[][4] = {
        {0.05f, 0.01f, 500.0f, 0.008f},
        {0.2f, 0.1f, 1000.0f, 0.033f},
        {0.15f, 0.05f, 800.0f, 0.016f},
    };
    for (auto& p : params)
    {
        float delta = ComputePCISPHDelta(p[0], p[1], p[2], p[3]);
        EXPECT_FALSE(std::isnan(delta)) << "h=" << p[0] << " m=" << p[1];
        EXPECT_FALSE(std::isinf(delta)) << "h=" << p[0] << " m=" << p[1];
    }
}
