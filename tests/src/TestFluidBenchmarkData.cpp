#include <gtest/gtest.h>

#include "Renderer/FluidBenchmarkData.h"

#include <cmath>

TEST(FluidBenchmarkData, GeneratesRequestedParticleCount)
{
    Engine::FluidBenchmarkInitialStateConfig config;
    config.ParticleCount = 1000;

    const auto particles = Engine::GenerateFluidBenchmarkParticles(config);

    ASSERT_EQ(particles.size(), 1000u);
    for (const auto& particle : particles)
    {
        EXPECT_FLOAT_EQ(particle.PositionAndLife.w, 1.0f);
        EXPECT_FLOAT_EQ(particle.VelocityAndMaxLife.w, 1.0f);
        EXPECT_TRUE(std::isfinite(particle.PositionAndLife.x));
        EXPECT_TRUE(std::isfinite(particle.PositionAndLife.y));
        EXPECT_TRUE(std::isfinite(particle.PositionAndLife.z));
    }
}

TEST(FluidBenchmarkData, SameSeedProducesSameState)
{
    Engine::FluidBenchmarkInitialStateConfig config;
    config.ParticleCount = 512;
    config.Seed          = 1234;

    const auto first  = Engine::GenerateFluidBenchmarkParticles(config);
    const auto second = Engine::GenerateFluidBenchmarkParticles(config);

    EXPECT_EQ(Engine::HashFluidBenchmarkParticles(first), Engine::HashFluidBenchmarkParticles(second));
}

TEST(FluidBenchmarkData, DifferentSeedChangesState)
{
    Engine::FluidBenchmarkInitialStateConfig config;
    config.ParticleCount = 512;
    config.Seed          = 1234;
    const auto first     = Engine::GenerateFluidBenchmarkParticles(config);

    config.Seed       = 1235;
    const auto second = Engine::GenerateFluidBenchmarkParticles(config);

    EXPECT_NE(Engine::HashFluidBenchmarkParticles(first), Engine::HashFluidBenchmarkParticles(second));
}

TEST(FluidBenchmarkData, InvalidConfigurationReturnsEmptyState)
{
    Engine::FluidBenchmarkInitialStateConfig config;
    config.ParticleCount = 128;
    config.Spacing       = 0.0f;

    EXPECT_TRUE(Engine::GenerateFluidBenchmarkParticles(config).empty());
}
