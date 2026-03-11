#pragma once

#include "Core/Base.h"
#include "Renderer/Shader.h"
#include "Renderer/SpatialHashGrid.h"
#include "Renderer/StorageBuffer.h"
#include "Renderer/VertexArray.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace Engine
{

    struct FluidEmitterComponent;

#ifdef ENGINE_ENABLE_CUDA
    class CudaGLInteropContext;
#endif

    class FluidSystemGPU
    {
    public:
        FluidSystemGPU(uint32_t particleCount);
        ~FluidSystemGPU();

        void Init();
        void Emit(const glm::vec3& emitterPos, const FluidEmitterComponent& emitter);
        void Update(float dt, const glm::vec3& emitterPos, const FluidEmitterComponent& emitter,
                    entt::registry* registry = nullptr);

        uint32_t GetParticleCount() const { return m_ParticleCount; }
        Ref<ShaderStorageBuffer> GetParticleBuffer() const { return m_ParticleBuffer; }
        Ref<VertexArray> GetEmptyVAO() const { return m_EmptyVAO; }

    private:
        uint32_t m_ParticleCount;

        // GPU buffers
        Ref<ShaderStorageBuffer> m_ParticleBuffer; // binding 0
        Ref<ShaderStorageBuffer> m_AliveList;      // binding 2 (identity: [0,1,...,N-1])

        // Fluid-specific shaders
        Ref<Shader> m_EmitShader;
        Ref<Shader> m_SimulateShader;

        // SPH shaders (reused from particle system)
        Ref<Shader> m_SPHDensityShader;
        Ref<Shader> m_SPHForceShader;

        // Spatial hash grid (reused)
        SpatialHashGrid m_Grid;

        // Empty VAO for instanced draw
        Ref<VertexArray> m_EmptyVAO;

        bool m_Initialized = false;
        bool m_SPHInitialized = false;
        float m_TotalTime = 0.0f;

        // PCISPH
        Ref<Shader> m_PCISPHInitShader;
        Ref<Shader> m_PCISPHPredictShader;
        Ref<Shader> m_PCISPHDensityShader;
        Ref<Shader> m_PCISPHForceShader;
        Ref<Shader> m_PCISPHApplyShader;

        Ref<ShaderStorageBuffer> m_PCISPHBuffer;    // 48B/particle
        Ref<ShaderStorageBuffer> m_RigidBodyBuffer; // 112B × MAX_RIGID_BODIES
        static constexpr uint32_t MAX_RIGID_BODIES = 64;
        bool m_PCISPHInitialized = false;

        void InitSPH(float smoothingRadius);
        void InitPCISPH();
        void InitRigidBodyBuffer();
        uint32_t UploadRigidBodies(entt::registry* registry);

#ifdef ENGINE_ENABLE_CUDA
        Scope<CudaGLInteropContext> m_CudaInterop;
        bool m_UseCudaPath = false;
        bool m_CudaInitAttempted = false;
        int m_CudaSlotParticle = -1;  // 仅 ParticleBuffer 注册 interop（渲染需要）
        void* m_CudaSPHCtx = nullptr; // CudaSPHContext（grid + PCISPH + rigidBody）
        void* m_CudaEventStart = nullptr;
        void* m_CudaEventStop = nullptr;
#endif
    };

} // namespace Engine
