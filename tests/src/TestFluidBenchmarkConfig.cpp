#include <gtest/gtest.h>

#include "Core/FluidBenchmarkConfig.h"

#include <string>
#include <vector>

namespace
{
    bool Parse(const std::vector<const char*>& args, Engine::FluidBenchmarkConfig& config, std::string& error)
    {
        return Engine::FluidBenchmarkConfig::Parse(static_cast<int>(args.size()), args.data(), config, error);
    }
} // namespace

TEST(FluidBenchmarkConfig, DefaultsWhenDisabled)
{
    const std::vector<const char*> args = {"Editor.exe"};
    Engine::FluidBenchmarkConfig   config;
    std::string                    error;

    ASSERT_TRUE(Parse(args, config, error)) << error;
    EXPECT_FALSE(config.Enabled);
    EXPECT_EQ(config.Backend, Engine::FluidBenchmarkBackend::OpenGL);
    EXPECT_EQ(config.Solver, Engine::FluidBenchmarkSolver::WCSPH);
    EXPECT_EQ(config.ParticleCount, 10000u);
    EXPECT_EQ(config.Iterations, 6u);
    EXPECT_EQ(config.WarmupFrames, 100u);
    EXPECT_EQ(config.SampleFrames, 1000u);
    EXPECT_EQ(config.Runs, 5u);
    EXPECT_FLOAT_EQ(config.FixedDeltaTime, 1.0f / 120.0f);
    EXPECT_EQ(config.Seed, 42u);
}

TEST(FluidBenchmarkConfig, ParsesCompleteConfiguration)
{
    const std::vector<const char*> args = {"Editor.exe",   "--benchmark-fluid",
                                           "--backend",    "opengl",
                                           "--solver",     "pcisph",
                                           "--particles",  "50000",
                                           "--iterations", "8",
                                           "--warmup",     "20",
                                           "--frames",     "200",
                                           "--runs",       "3",
                                           "--fixed-dt",   "0.01",
                                           "--seed",       "7",
                                           "--output",     "benchmark/custom.csv"};
    Engine::FluidBenchmarkConfig   config;
    std::string                    error;

    ASSERT_TRUE(Parse(args, config, error)) << error;
    EXPECT_TRUE(config.Enabled);
    EXPECT_EQ(config.Backend, Engine::FluidBenchmarkBackend::OpenGL);
    EXPECT_EQ(config.Solver, Engine::FluidBenchmarkSolver::PCISPH);
    EXPECT_EQ(config.ParticleCount, 50000u);
    EXPECT_EQ(config.Iterations, 8u);
    EXPECT_EQ(config.WarmupFrames, 20u);
    EXPECT_EQ(config.SampleFrames, 200u);
    EXPECT_EQ(config.Runs, 3u);
    EXPECT_FLOAT_EQ(config.FixedDeltaTime, 0.01f);
    EXPECT_EQ(config.Seed, 7u);
    EXPECT_EQ(config.OutputPath, "benchmark/custom.csv");
}

TEST(FluidBenchmarkConfig, RejectsUnknownBackend)
{
    const std::vector<const char*> args = {"Editor.exe", "--benchmark-fluid", "--backend", "metal"};
    Engine::FluidBenchmarkConfig   config;
    std::string                    error;

    EXPECT_FALSE(Parse(args, config, error));
    EXPECT_NE(error.find("--backend"), std::string::npos);
}

TEST(FluidBenchmarkConfig, RejectsMissingValue)
{
    const std::vector<const char*> args = {"Editor.exe", "--benchmark-fluid", "--particles"};
    Engine::FluidBenchmarkConfig   config;
    std::string                    error;

    EXPECT_FALSE(Parse(args, config, error));
    EXPECT_NE(error.find("requires a value"), std::string::npos);
}

TEST(FluidBenchmarkConfig, RejectsInvalidRanges)
{
    const std::vector<const char*> args = {"Editor.exe", "--benchmark-fluid", "--frames", "0"};
    Engine::FluidBenchmarkConfig   config;
    std::string                    error;

    EXPECT_FALSE(Parse(args, config, error));
    EXPECT_NE(error.find("supported range"), std::string::npos);
}

TEST(FluidBenchmarkConfig, RejectsLegacyVulkanConflict)
{
    const std::vector<const char*> args = {"Editor.exe", "--benchmark-fluid", "--backend", "opengl", "--vulkan"};
    Engine::FluidBenchmarkConfig   config;
    std::string                    error;

    EXPECT_FALSE(Parse(args, config, error));
    EXPECT_NE(error.find("conflicts"), std::string::npos);
}
