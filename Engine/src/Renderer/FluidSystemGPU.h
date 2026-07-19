#pragma once

#include "Core/Base.h"
#include "Renderer/GPUAsyncReadback.h"
#include "Renderer/FluidBenchmarkData.h"
#include "Renderer/Shader.h"
#include "Renderer/SPHCommon.h"
#include "Renderer/SpatialHashGrid.h"
#include "Renderer/StorageBuffer.h"
#include "Renderer/UniformBuffer.h"
#include "Renderer/VertexArray.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Engine
{

    struct FluidEmitterComponent;

    enum class FluidComputeBackend : uint8_t
    {
        Automatic = 0,
        OpenGL,
        CUDA,
        Vulkan
    };

    struct FluidComputeTimingSample
    {
        FluidComputeBackend Backend   = FluidComputeBackend::Automatic;
        float               ComputeMs = 0.0f;
        float               InteropMs = 0.0f;
        uint64_t            Sequence  = 0;
        bool                Valid     = false;
    };

    class FluidSystemGPU
    {
    public:
        struct MeshSDFDebugBody
        {
            glm::vec3 Center      = glm::vec3(0.0f);
            glm::vec3 HalfExtents = glm::vec3(0.0f);
            glm::vec3 Rotation    = glm::vec3(0.0f);
            uint32_t  Resolution  = 0;
            uint32_t  VoxelCount  = 0;
            float     Band        = 0.0f;
            float     Blend       = 0.0f;
        };

        struct MeshSDFDebugStats
        {
            uint32_t BodyCount        = 0;
            uint32_t VoxelCount       = 0;
            uint32_t EstimatedSamples = 0;
            uint32_t Resolution       = 0;
            float    Band             = 0.0f;
            float    LastBuildCpuMs   = 0.0f;
            bool     Enabled          = false;
        };

        explicit FluidSystemGPU(uint32_t particleCount, FluidComputeBackend backend = FluidComputeBackend::Automatic);
        ~FluidSystemGPU();

        void Init();
        void Emit(const glm::vec3& emitterPos, const FluidEmitterComponent& emitter);
        void Update(float                        dt,
                    const glm::vec3&             emitterPos,
                    const FluidEmitterComponent& emitter,
                    entt::registry*              registry = nullptr);
        bool SetBenchmarkParticles(const std::vector<FluidBenchmarkParticle>& particles);
        bool ReadBenchmarkParticles(std::vector<FluidBenchmarkParticle>& particles, std::string& error) const;

        uint32_t                             GetParticleCount() const { return m_ParticleCount; }
        Ref<ShaderStorageBuffer>             GetParticleBuffer() const { return m_ParticleBuffer; }
        Ref<VertexArray>                     GetEmptyVAO() const { return m_EmptyVAO; }
        const std::vector<MeshSDFDebugBody>& GetMeshSDFDebugBodies() const { return m_MeshSDFDebugBodies; }
        const MeshSDFDebugStats&             GetMeshSDFDebugStats() const { return m_MeshSDFDebugStats; }
        FluidComputeBackend                  GetRequestedBackend() const { return m_RequestedBackend; }
        FluidComputeBackend                  GetActiveBackend() const { return m_ActiveBackend; }
        bool                                 IsBackendReady() const { return m_BackendReady; }
        const std::string&                   GetBackendFailureReason() const { return m_BackendFailureReason; }
        const FluidComputeTimingSample&      GetLastTimingSample() const { return m_LastTimingSample; }

        static const char* BackendLabel(FluidComputeBackend backend);

    private:
        uint32_t                 m_ParticleCount;
        FluidComputeBackend      m_RequestedBackend = FluidComputeBackend::Automatic;
        FluidComputeBackend      m_ActiveBackend    = FluidComputeBackend::Automatic;
        bool                     m_BackendReady     = false;
        std::string              m_BackendFailureReason;
        FluidComputeTimingSample m_LastTimingSample;

        // GPU buffers
        Ref<ShaderStorageBuffer> m_ParticleBuffer; // binding 0
        Ref<ShaderStorageBuffer> m_AliveList;      // binding 2
        Ref<ShaderStorageBuffer> m_DeadList;       // binding 12
        Ref<ShaderStorageBuffer> m_CounterBuffer;  // binding 13 {deadCount, aliveCount, emitCount, pad}

        // Fluid-specific shaders
        Ref<Shader> m_EmitShader;
        Ref<Shader> m_SimulateShader;
        Ref<Shader> m_CompactShader;

        // SPH shaders (共享结构体)
        SPHShaderSet m_SPHShaders;

        // Spatial hash grid (reused)
        SpatialHashGrid m_Grid;

        // Empty VAO for instanced draw
        Ref<VertexArray> m_EmptyVAO;

        bool  m_Initialized    = false;
        bool  m_SPHInitialized = false;
        float m_TotalTime      = 0.0f;

        // Lifetime mode state
        float    m_EmitAccumulator = 0.0f;
        uint32_t m_LastAliveCount  = 0;

        // PCISPH
        Ref<ShaderStorageBuffer> m_PCISPHBuffer;        // 48B/particle
        Ref<ShaderStorageBuffer> m_RigidBodyBuffer;     // 112B × MAX_RIGID_BODIES
        Ref<ShaderStorageBuffer> m_MeshSDFMetaBuffer;   // Mesh SDF 元数据, binding 10
        Ref<ShaderStorageBuffer> m_MeshSDFVoxelBuffer;  // Mesh SDF 体素值, binding 11
        Ref<ShaderStorageBuffer> m_SurfaceNormalBuffer; // vec4/particle, binding 8 (Akinci 表面法线)
        bool                     m_PCISPHInitialized = false;

        // 调试可视化数据
        std::vector<MeshSDFDebugBody> m_MeshSDFDebugBodies;
        MeshSDFDebugStats             m_MeshSDFDebugStats;

        // MeshSDF 缓存：当碰撞体 Transform 未变化时跳过重建
        size_t              m_MeshSDFCacheHash = 0;
        MeshSDFUploadResult m_CachedMeshSDFResult;
        bool                m_MeshSDFCacheValid = false;

        void InitSPH(float smoothingRadius);
        void InitPCISPH();
        void InitRigidBodyBuffer();
        void InitMeshSDFBuffer();

        // CUDA 路径 Update 分派（成功返回 true，失败时 Update fall through 到 GL）
        bool UpdateCuda(float                        clampedDt,
                        const glm::vec3&             emitterPos,
                        const FluidEmitterComponent& emitter,
                        entt::registry*              registry,
                        const SPHKernelParams&       kp);

        // CUDA compute sidecar（Pimpl 模式隐藏 CUDA 依赖）
        struct CudaImpl;
        Scope<CudaImpl> m_CudaImpl;

        // ---- Vulkan 路径（emit + simulate；SPH 段 Commit C 接通）----
        // Vulkan 资源以 Pimpl 隐藏在 .cpp，避免 .h 引入 Vulkan 头依赖。
        struct VulkanResources;
        Scope<VulkanResources> m_VulkanResources;

        // Emit / Simulate 走 UBO 上传大块参数（仅 Vulkan 路径）
        Ref<UniformBuffer> m_EmitParamsUBO;
        Ref<UniformBuffer> m_SimParamsUBO;

        // SPH 路径统一 UBO：stable params + PCISPH delta（binding=12）
        // 7 SPH shader 共享同一份布局，每 dispatch 仅小常量走 push
        // constant（u_AliveCount/u_DeltaTime/u_UsePredictedPos）
        Ref<UniformBuffer> m_SPHParamsUBO;

        // MeshSDFMeta 异步回读（Commit D 预埋；当前 SPH 段在 Vulkan 跳过，跑不到）
        Ref<GPUAsyncReadback> m_SDFMetaReadback;

        // Vulkan 路径 Update 分派
        void UpdateVulkan(float                        dt,
                          const glm::vec3&             emitterPos,
                          const FluidEmitterComponent& emitter,
                          entt::registry*              registry);
        bool InitVulkanComputeResources();
        bool InitSPHVulkanPipelines();
        void DestroyVulkanComputeResources();
        void PublishTimingSample(FluidComputeBackend backend, float computeMs, float interopMs = 0.0f);
    };

} // namespace Engine
