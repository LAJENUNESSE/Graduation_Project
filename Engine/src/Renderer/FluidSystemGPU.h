#pragma once

#include "Core/Base.h"
#include "Renderer/GPUAsyncReadback.h"
#include "Renderer/Shader.h"
#include "Renderer/SPHCommon.h"
#include "Renderer/SpatialHashGrid.h"
#include "Renderer/StorageBuffer.h"
#include "Renderer/UniformBuffer.h"
#include "Renderer/VertexArray.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vector>

namespace Engine
{

    struct FluidEmitterComponent;

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

        FluidSystemGPU(uint32_t particleCount);
        ~FluidSystemGPU();

        void Init();
        void Emit(const glm::vec3& emitterPos, const FluidEmitterComponent& emitter);
        void Update(float                        dt,
                    const glm::vec3&             emitterPos,
                    const FluidEmitterComponent& emitter,
                    entt::registry*              registry = nullptr);

        uint32_t                             GetParticleCount() const { return m_ParticleCount; }
        Ref<ShaderStorageBuffer>             GetParticleBuffer() const { return m_ParticleBuffer; }
        Ref<VertexArray>                     GetEmptyVAO() const { return m_EmptyVAO; }
        const std::vector<MeshSDFDebugBody>& GetMeshSDFDebugBodies() const { return m_MeshSDFDebugBodies; }
        const MeshSDFDebugStats&             GetMeshSDFDebugStats() const { return m_MeshSDFDebugStats; }

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
        Ref<ShaderStorageBuffer> m_MeshSDFMetaBuffer;   // Mesh SDF 元数据, binding 10
        Ref<ShaderStorageBuffer> m_MeshSDFVoxelBuffer;  // Mesh SDF 体素值, binding 11
        Ref<ShaderStorageBuffer> m_SurfaceNormalBuffer; // vec4/particle, binding 8 (Akinci 表面法线)
        bool                     m_PCISPHInitialized = false;

        // 调试可视化数据
        std::vector<MeshSDFDebugBody> m_MeshSDFDebugBodies;
        MeshSDFDebugStats             m_MeshSDFDebugStats;

        void InitSPH(float smoothingRadius);
        void InitPCISPH();
        void InitRigidBodyBuffer();
        void InitMeshSDFBuffer();

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
    };

} // namespace Engine
