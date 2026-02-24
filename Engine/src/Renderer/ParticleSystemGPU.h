#pragma once

#include "Core/Base.h"
#include "Renderer/Shader.h"
#include "Renderer/StorageBuffer.h"
#include "Renderer/VertexArray.h"

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

        // Empty VAO for indirect draw
        Ref<VertexArray> m_EmptyVAO;

        bool m_Initialized = false;
        float m_EmitAccumulator = 0.0f;
    };

} // namespace Engine
