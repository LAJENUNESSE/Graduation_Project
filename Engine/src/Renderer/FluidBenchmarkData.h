#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Engine
{

    struct FluidBenchmarkParticle
    {
        glm::vec4 PositionAndLife{0.0f};
        glm::vec4 VelocityAndMaxLife{0.0f};
        glm::vec4 StartColor{0.0f};
        glm::vec4 EndColor{0.0f};
        glm::vec4 Params{0.0f};
    };

    static_assert(sizeof(FluidBenchmarkParticle) == 80, "Fluid benchmark particle layout must match GLSL/CUDA");

    struct FluidBenchmarkInitialStateConfig
    {
        uint32_t  ParticleCount = 10000;
        uint32_t  Seed          = 42;
        glm::vec3 Center{0.0f};
        glm::vec3 InitialVelocity{0.0f};
        float     Spacing        = 0.05f;
        float     JitterFraction = 0.05f;
    };

    std::vector<FluidBenchmarkParticle> GenerateFluidBenchmarkParticles(const FluidBenchmarkInitialStateConfig& config);

    uint64_t HashFluidBenchmarkParticles(const std::vector<FluidBenchmarkParticle>& particles);

} // namespace Engine
