#pragma once

#include "Core/Base.h"
#include "Renderer/Shader.h"
#include "Renderer/StorageBuffer.h"
#include "Renderer/VertexArray.h"
#include "Renderer/SpatialHashGrid.h"

#include <glm/glm.hpp>

namespace Engine
{

    struct ParticleEmitterComponent;

    class ParticleSystemGPU
    {
    public:
        ParticleSystemGPU(uint32_t maxParticles);
        ~ParticleSystemGPU() = default;

        void Init();
        void Update(float dt, const glm::vec3& emitterPos, const ParticleEmitterComponent& emitter);
        void Render(const glm::mat4& viewMatrix, const glm::mat4& projection);

        uint32_t GetMaxParticles() const { return m_MaxParticles; }

    private:
        uint32_t m_MaxParticles;

        // GPU buffers
        Ref<ShaderStorageBuffer> m_ParticleBuffer;   // binding 0
        Ref<ShaderStorageBuffer> m_DeadList;          // binding 1
        Ref<ShaderStorageBuffer> m_AliveList;         // binding 2
        Ref<ShaderStorageBuffer> m_CounterBuffer;     // binding 3
        Ref<ShaderStorageBuffer> m_IndirectArgs;      // binding 4

        // Shaders
        Ref<Shader> m_EmitShader;
        Ref<Shader> m_SimulateShader;
        Ref<Shader> m_RenderArgsShader;
        Ref<Shader> m_BillboardShader;

        // SPH shaders
        Ref<Shader> m_SPHDensityShader;
        Ref<Shader> m_SPHForceShader;

        // Spatial hash grid for SPH
        SpatialHashGrid m_Grid;

        // Empty VAO for indirect draw
        Ref<VertexArray> m_EmptyVAO;

        bool m_Initialized = false;
        bool m_SPHInitialized = false;
        float m_EmitAccumulator = 0.0f;

        // 上一帧的活跃粒子数（用于 SPH dispatch）
        uint32_t m_LastAliveCount = 0;

        void InitSPH(float smoothingRadius);
    };

} // namespace Engine
