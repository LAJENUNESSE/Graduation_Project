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

    struct ParticleEmitterComponent;

#ifdef ENGINE_ENABLE_CUDA
    class CudaGLInteropContext;
#endif

    class ParticleSystemGPU
    {
    public:
        ParticleSystemGPU(uint32_t maxParticles);
        ~ParticleSystemGPU();

        void Init();
        void Update(float                           dt,
                    const glm::vec3&                emitterPos,
                    const ParticleEmitterComponent& emitter,
                    entt::registry*                 registry = nullptr);
        void Render(const glm::mat4& viewMatrix, const glm::mat4& projection);

        uint32_t GetMaxParticles() const { return m_MaxParticles; }

    private:
        uint32_t m_MaxParticles;

        // GPU buffers
        Ref<ShaderStorageBuffer> m_ParticleBuffer; // binding 0
        Ref<ShaderStorageBuffer> m_DeadList;       // binding 1
        Ref<ShaderStorageBuffer> m_AliveList;      // binding 2
        Ref<ShaderStorageBuffer> m_CounterBuffer;  // binding 3
        Ref<ShaderStorageBuffer> m_IndirectArgs;   // binding 4

        // Shaders
        Ref<Shader> m_EmitShader;
        Ref<Shader> m_SimulateShader;
        Ref<Shader> m_CompactShader;
        Ref<Shader> m_RenderArgsShader;
        Ref<Shader> m_BillboardShader;

        // SPH shaders (共享结构体)
        SPHShaderSet m_SPHShaders;

        // Spatial hash grid for SPH
        SpatialHashGrid m_Grid;

        // Empty VAO for indirect draw
        Ref<VertexArray> m_EmptyVAO;

        bool m_Initialized    = false;
        bool m_SPHInitialized = false;

        // PCISPH
        Ref<ShaderStorageBuffer> m_PCISPHBuffer;    // binding 1 during SPH, 48B/particle
        Ref<ShaderStorageBuffer> m_RigidBodyBuffer; // binding 3 during SPH, 112B × MAX_RIGID_BODIES
        bool                     m_PCISPHInitialized = false;

        void InitPCISPH();
        void InitRigidBodyBuffer();

        float m_EmitAccumulator = 0.0f;
        float m_TotalTime       = 0.0f;

        // VMware/Mesa compatibility fallback:
        // use direct instanced draw instead of DrawArraysIndirect.
        bool     m_UseIndirectDraw         = true;
        bool     m_VMwareCompatMode        = false;
        bool     m_DisableSPHOnDriver      = false;
        bool     m_SPHDisableLogged        = false;
        uint32_t m_AliveCountForDirectDraw = 0;

        // 上一帧的活跃粒子数（用于 SPH dispatch）
        uint32_t m_LastAliveCount = 0;

        // 异步回读（避免 glGetBufferSubData 同步阻塞）
        uint32_t m_ReadbackBuffer  = 0;       // GL buffer for async copy
        void*    m_ReadbackFence   = nullptr; // GLsync fence
        bool     m_ReadbackPending = false;

#ifdef ENGINE_ENABLE_CUDA
        // CUDA compute sidecar（Phase 1: emit / simulate / render_args）
        Scope<CudaGLInteropContext> m_CudaInterop;
        bool                        m_UseCudaPath       = false;
        bool                        m_CudaInitAttempted = false;
        int                         m_CudaSlotParticle  = -1;
        int                         m_CudaSlotDeadList  = -1;
        int                         m_CudaSlotAliveList = -1;
        int                         m_CudaSlotCounter   = -1;
        int                         m_CudaSlotIndirect  = -1;

        // CUDA event 计时（Ping-pong 双缓冲）
        CudaTimingHelper m_CudaTiming;

        // CUDA SPH context（grid + PCISPH + rigidBody 缓冲区）
        void* m_CudaSPHCtx = nullptr;
#endif

        void InitSPH(float smoothingRadius);
    };

} // namespace Engine
