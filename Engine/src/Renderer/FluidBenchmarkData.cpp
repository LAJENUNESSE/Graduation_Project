#include "engpch.h"
#include "Renderer/FluidBenchmarkData.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Engine
{

    namespace
    {
        uint32_t PCGHash(uint32_t value)
        {
            const uint32_t state = value * 747796405u + 2891336453u;
            const uint32_t word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
            return (word >> 22u) ^ word;
        }

        float SignedUnitRandom(uint32_t& state)
        {
            state                 = PCGHash(state);
            const float zeroToOne = static_cast<float>(state) / 4294967296.0f;
            return zeroToOne * 2.0f - 1.0f;
        }
    } // namespace

    std::vector<FluidBenchmarkParticle> GenerateFluidBenchmarkParticles(const FluidBenchmarkInitialStateConfig& config)
    {
        if (config.ParticleCount == 0 || !std::isfinite(config.Spacing) || config.Spacing <= 0.0f ||
            !std::isfinite(config.JitterFraction) || config.JitterFraction < 0.0f)
        {
            return {};
        }

        const uint32_t  side   = static_cast<uint32_t>(std::ceil(std::cbrt(static_cast<double>(config.ParticleCount))));
        const float     extent = static_cast<float>(side - 1) * config.Spacing;
        const glm::vec3 origin = config.Center - glm::vec3(extent * 0.5f);
        const float     jitterAmplitude = config.Spacing * std::min(config.JitterFraction, 0.49f);

        std::vector<FluidBenchmarkParticle> particles(config.ParticleCount);
        for (uint32_t index = 0; index < config.ParticleCount; ++index)
        {
            const uint32_t x = index % side;
            const uint32_t y = (index / side) % side;
            const uint32_t z = index / (side * side);

            uint32_t        randomState = PCGHash(config.Seed ^ index);
            const glm::vec3 jitter =
                glm::vec3(SignedUnitRandom(randomState), SignedUnitRandom(randomState), SignedUnitRandom(randomState)) *
                jitterAmplitude;
            const glm::vec3 position =
                origin +
                glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) * config.Spacing +
                jitter;

            auto& particle              = particles[index];
            particle.PositionAndLife    = glm::vec4(position, 1.0f);
            particle.VelocityAndMaxLife = glm::vec4(config.InitialVelocity, 1.0f);
        }

        return particles;
    }

    uint64_t HashFluidBenchmarkParticles(const std::vector<FluidBenchmarkParticle>& particles)
    {
        constexpr uint64_t fnvOffset = 14695981039346656037ull;
        constexpr uint64_t fnvPrime  = 1099511628211ull;

        uint64_t     hash      = fnvOffset;
        const auto*  bytes     = reinterpret_cast<const uint8_t*>(particles.data());
        const size_t byteCount = particles.size() * sizeof(FluidBenchmarkParticle);
        for (size_t i = 0; i < byteCount; ++i)
        {
            hash ^= bytes[i];
            hash *= fnvPrime;
        }
        return hash;
    }

} // namespace Engine
