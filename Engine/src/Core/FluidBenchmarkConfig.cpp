#include "engpch.h"
#include "Core/FluidBenchmarkConfig.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <string_view>

namespace Engine
{

    namespace
    {
        FluidBenchmarkConfig s_Config;

        bool ParseUInt32(std::string_view text, uint32_t& outValue)
        {
            if (text.empty())
                return false;

            uint32_t value          = 0;
            const auto [ptr, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || ptr != text.data() + text.size())
                return false;

            outValue = value;
            return true;
        }

        bool ParseFloat(std::string_view text, float& outValue)
        {
            if (text.empty())
                return false;

            float value             = 0.0f;
            const auto [ptr, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || ptr != text.data() + text.size() || !std::isfinite(value))
                return false;

            outValue = value;
            return true;
        }

        bool ReadValue(int                argc,
                       const char* const* argv,
                       int&               index,
                       std::string_view   option,
                       std::string_view&  outValue,
                       std::string&       outError)
        {
            if (index + 1 >= argc)
            {
                outError = std::string(option) + " requires a value";
                return false;
            }

            outValue = argv[++index];
            return true;
        }
    } // namespace

    bool FluidBenchmarkConfig::Parse(int                   argc,
                                     const char* const*    argv,
                                     FluidBenchmarkConfig& outConfig,
                                     std::string&          outError)
    {
        outConfig = FluidBenchmarkConfig{};
        outError.clear();

        bool legacyVulkanFlag = false;

        for (int i = 1; i < argc; ++i)
        {
            const std::string_view option = argv[i];

            if (option == "--benchmark-fluid")
            {
                outConfig.Enabled = true;
                continue;
            }
            if (option == "--vulkan")
            {
                legacyVulkanFlag = true;
                continue;
            }
            if (option == "--benchmark-help")
            {
                outConfig.Enabled = true;
                continue;
            }

            const bool benchmarkOption = option == "--backend" || option == "--solver" || option == "--particles" ||
                                         option == "--iterations" || option == "--warmup" || option == "--frames" ||
                                         option == "--runs" || option == "--fixed-dt" || option == "--seed" ||
                                         option == "--output";
            if (!benchmarkOption)
                continue;

            outConfig.Enabled = true;
            std::string_view value;
            if (!ReadValue(argc, argv, i, option, value, outError))
                return false;

            if (option == "--backend")
            {
                if (value == "opengl")
                    outConfig.Backend = FluidBenchmarkBackend::OpenGL;
                else if (value == "cuda")
                    outConfig.Backend = FluidBenchmarkBackend::CUDA;
                else if (value == "vulkan")
                    outConfig.Backend = FluidBenchmarkBackend::Vulkan;
                else
                {
                    outError = "--backend must be one of: opengl, cuda, vulkan";
                    return false;
                }
            }
            else if (option == "--solver")
            {
                if (value == "wcsph")
                    outConfig.Solver = FluidBenchmarkSolver::WCSPH;
                else if (value == "pcisph")
                    outConfig.Solver = FluidBenchmarkSolver::PCISPH;
                else
                {
                    outError = "--solver must be one of: wcsph, pcisph";
                    return false;
                }
            }
            else if (option == "--output")
            {
                if (value.empty())
                {
                    outError = "--output must not be empty";
                    return false;
                }
                outConfig.OutputPath = value;
            }
            else if (option == "--fixed-dt")
            {
                if (!ParseFloat(value, outConfig.FixedDeltaTime) || outConfig.FixedDeltaTime <= 0.0f ||
                    outConfig.FixedDeltaTime > 1.0f)
                {
                    outError = "--fixed-dt must be a finite value in (0, 1]";
                    return false;
                }
            }
            else
            {
                uint32_t* target  = nullptr;
                uint32_t  minimum = 0;
                uint32_t  maximum = std::numeric_limits<uint32_t>::max();

                if (option == "--particles")
                {
                    target  = &outConfig.ParticleCount;
                    minimum = 1;
                    maximum = 10000000;
                }
                else if (option == "--iterations")
                {
                    target  = &outConfig.Iterations;
                    minimum = 1;
                    maximum = 64;
                }
                else if (option == "--warmup")
                {
                    target  = &outConfig.WarmupFrames;
                    maximum = 1000000;
                }
                else if (option == "--frames")
                {
                    target  = &outConfig.SampleFrames;
                    minimum = 1;
                    maximum = 10000000;
                }
                else if (option == "--runs")
                {
                    target  = &outConfig.Runs;
                    minimum = 1;
                    maximum = 1000;
                }
                else if (option == "--seed")
                {
                    target = &outConfig.Seed;
                }

                uint32_t parsed = 0;
                if (!target || !ParseUInt32(value, parsed) || parsed < minimum || parsed > maximum)
                {
                    outError = std::string(option) + " is outside the supported range";
                    return false;
                }
                *target = parsed;
            }
        }

        if (outConfig.Enabled && legacyVulkanFlag && outConfig.Backend != FluidBenchmarkBackend::Vulkan)
        {
            outError = "--vulkan conflicts with a non-Vulkan benchmark backend";
            return false;
        }

        if (outConfig.Backend == FluidBenchmarkBackend::CUDA)
        {
#ifndef ENGINE_ENABLE_CUDA
            outError = "CUDA benchmark requested, but ENGINE_ENABLE_CUDA is disabled in this build";
            return false;
#endif
        }

        if (outConfig.Backend == FluidBenchmarkBackend::Vulkan)
        {
#ifndef ENGINE_ENABLE_VULKAN
            outError = "Vulkan benchmark requested, but ENGINE_ENABLE_VULKAN is disabled in this build";
            return false;
#endif
        }

        return true;
    }

    bool FluidBenchmarkConfig::Initialize(int argc, const char* const* argv, std::string& outError)
    {
        FluidBenchmarkConfig parsed;
        if (!Parse(argc, argv, parsed, outError))
            return false;

        s_Config = std::move(parsed);
        return true;
    }

    const FluidBenchmarkConfig& FluidBenchmarkConfig::Get()
    {
        return s_Config;
    }

    const char* FluidBenchmarkConfig::BackendLabel(FluidBenchmarkBackend backend)
    {
        switch (backend)
        {
        case FluidBenchmarkBackend::OpenGL:
            return "OpenGL";
        case FluidBenchmarkBackend::CUDA:
            return "CUDA";
        case FluidBenchmarkBackend::Vulkan:
            return "Vulkan";
        default:
            return "Unknown";
        }
    }

    const char* FluidBenchmarkConfig::SolverLabel(FluidBenchmarkSolver solver)
    {
        switch (solver)
        {
        case FluidBenchmarkSolver::WCSPH:
            return "WCSPH";
        case FluidBenchmarkSolver::PCISPH:
            return "PCISPH";
        default:
            return "Unknown";
        }
    }

    const char* FluidBenchmarkConfig::Usage()
    {
        return "--benchmark-fluid --backend <opengl|cuda|vulkan> --solver <wcsph|pcisph> "
               "--particles <N> --iterations <N> --warmup <N> --frames <N> --runs <N> "
               "--fixed-dt <seconds> --seed <N> --output <path>";
    }

} // namespace Engine
