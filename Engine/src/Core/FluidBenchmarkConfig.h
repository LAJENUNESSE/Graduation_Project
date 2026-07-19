#pragma once

#include <cstdint>
#include <string>

namespace Engine
{

    enum class FluidBenchmarkBackend : uint8_t
    {
        OpenGL = 0,
        CUDA,
        Vulkan
    };

    enum class FluidBenchmarkSolver : uint8_t
    {
        WCSPH = 0,
        PCISPH
    };

    struct FluidBenchmarkConfig
    {
        bool                  Enabled        = false;
        FluidBenchmarkBackend Backend        = FluidBenchmarkBackend::OpenGL;
        FluidBenchmarkSolver  Solver         = FluidBenchmarkSolver::WCSPH;
        uint32_t              ParticleCount  = 10000;
        uint32_t              Iterations     = 6;
        uint32_t              WarmupFrames   = 100;
        uint32_t              SampleFrames   = 1000;
        uint32_t              Runs           = 5;
        float                 FixedDeltaTime = 1.0f / 120.0f;
        uint32_t              Seed           = 42;
        std::string           OutputPath     = "benchmark/results.csv";

        static bool Parse(int argc, const char* const* argv, FluidBenchmarkConfig& outConfig, std::string& outError);
        static bool Initialize(int argc, const char* const* argv, std::string& outError);
        static const FluidBenchmarkConfig& Get();

        static const char* BackendLabel(FluidBenchmarkBackend backend);
        static const char* SolverLabel(FluidBenchmarkSolver solver);
        static const char* Usage();
    };

} // namespace Engine
