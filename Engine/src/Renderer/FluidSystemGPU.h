#pragma once

#include "Core/Base.h"
#include "Renderer/Shader.h"
#include "Renderer/SPHCommon.h"
#include "Renderer/SpatialHashGrid.h"
#include "Renderer/StorageBuffer.h"
#include "Renderer/VertexArray.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace Engine
{

    struct FluidEmitterComponent;

    class FluidSystemGPU
    {
    public:
        FluidSystemGPU(uint32_t particleCount);
        ~FluidSystemGPU();

        void Init();
        void Emit(const glm::vec3& emitterPos, const FluidEmitterComponent& emitter);
        void Update(float                        dt,
                    const glm::vec3&             emitterPos,
                    const FluidEmitterComponent& emitter,
                    entt::registry*              registry = nullptr);

        uint32_t                 GetParticleCount() const { return m_ParticleCount; }
        Ref<ShaderStorageBuffer> GetParticleBuffer() const { return m_ParticleBuffer; }
        Ref<VertexArray>         GetEmptyVAO() const { return m_EmptyVAO; }

    private:
        uint32_t m_ParticleCount;

        // GPU buffers
        Ref<ShaderStorageBuffer> m_ParticleBuffer; // binding 0
        Ref<ShaderStorageBuffer> m_AliveList;      // binding 2 (identity: [0,1,...,N-1])

        // Fluid-specific shaders
        Ref<Shader> m_EmitShader;
        Ref<Shader> m_SimulateShader;

        // SPH shaders (共享结构体)
        SPHShaderSet m_SPHShaders;

        // Spatial hash grid (reused)
        SpatialHashGrid m_Grid;

        // Empty VAO for instanced draw
        Ref<VertexArray> m_EmptyVAO;

        bool  m_Initialized    = false;
        bool  m_SPHInitialized = false;
        float m_TotalTime      = 0.0f;

        // PCISPH
        Ref<ShaderStorageBuffer> m_PCISPHBuffer;        // 48B/particle
        Ref<ShaderStorageBuffer> m_RigidBodyBuffer;     // 112B × MAX_RIGID_BODIES
        Ref<ShaderStorageBuffer> m_SurfaceNormalBuffer; // vec4/particle, binding 8 (Akinci 表面法线)
        bool                     m_PCISPHInitialized = false;

        void InitSPH(float smoothingRadius);
        void InitPCISPH();
        void InitRigidBodyBuffer();

        // CUDA compute sidecar（Pimpl 模式隐藏 CUDA 依赖）
        struct CudaImpl;
        Scope<CudaImpl> m_CudaImpl;
    };

} // namespace Engine
