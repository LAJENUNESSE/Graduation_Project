#include "engpch.h"
#include "Renderer/FluidSystemGPU.h"
#include "Renderer/SPHCommon.h"
#include "Renderer/SPHKernelMath.h"
#include "Core/Log.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/RendererAPI.h"
#include "Scene/Components.h"

#include <cmath>
#include <chrono>

#include "Debug/PerformanceMonitor.h"

#ifdef ENGINE_ENABLE_VULKAN
#include "Core/Assert.h"
#include "Platform/Vulkan/VulkanBarrierUtil.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDescriptor.h"
#include "Platform/Vulkan/VulkanPipeline.h"
#include "Platform/Vulkan/VulkanShader.h"

#include <vulkan/vulkan.h>
#endif

namespace Engine
{

    // =========================================================================
    // CudaImpl stub (CUDA removed)
    // =========================================================================
    struct FluidSystemGPU::CudaImpl
    {
    }; // Empty stub

    // Must match GLSL struct layout: 5 x vec4 = 80 bytes
    struct FluidGPUParticle
    {
        glm::vec4 posAndLife;
        glm::vec4 velAndMaxLife;
        glm::vec4 startColor;
        glm::vec4 endColor;
        glm::vec4 params; // z=density(SPH), w=pressure(SPH)
    };

#ifdef ENGINE_ENABLE_VULKAN
    namespace
    {
        // std140 UBO 镜像 — 与 fluid_emit.glsl 中 EmitParams 块逐字节对应（48B）
        struct alignas(16) FluidEmitParamsUBO
        {
            glm::vec4 EmitterPosV;      // xyz=EmitterPos, w=pad
            glm::vec4 EmitExtentsV;     // xyz=EmitExtents, w=pad
            glm::vec4 InitialVelocityV; // xyz=InitialVelocity, w=pad
        };
        static_assert(sizeof(FluidEmitParamsUBO) == 48, "FluidEmitParamsUBO must be std140-aligned 48 bytes");

        // std140 UBO 镜像 — 与 fluid_simulate.glsl 中 SimParams 块对应（48B）
        struct alignas(16) FluidSimParamsUBO
        {
            glm::vec4 GravityAndDamping;     // xyz=Gravity, w=Damping
            glm::vec4 BoundaryMinAndUseFlag; // xyz=BoundaryMin, w=UseBoundary(1.0/0.0)
            glm::vec4 BoundaryMaxAndMode;    // xyz=BoundaryMax, w=PCISPHMode(1.0/0.0)
        };
        static_assert(sizeof(FluidSimParamsUBO) == 48, "FluidSimParamsUBO must be std140-aligned 48 bytes");

        // Push constant 布局：与 fluid_emit.glsl PushConstants 一致（8B）
        struct FluidEmitPC
        {
            uint32_t ParticleCount;
            float    Time;
        };
        static_assert(sizeof(FluidEmitPC) == 8, "FluidEmitPC must be 8 bytes");

        // Push constant 布局：与 fluid_simulate.glsl PushConstants 一致（8B）
        struct FluidSimulatePC
        {
            float    DeltaTime;
            uint32_t ParticleCount;
        };
        static_assert(sizeof(FluidSimulatePC) == 8, "FluidSimulatePC must be 8 bytes");

        // Binding 常量（与 shader layout 一致）
        constexpr uint32_t FLUID_EMIT_UBO_BINDING = 5;
        constexpr uint32_t FLUID_SIM_UBO_BINDING  = 6;
    } // namespace

    // ============================================================
    // Vulkan 资源（Pimpl）— 仅 ENGINE_ENABLE_VULKAN 时存在
    // ============================================================
    struct FluidSystemGPU::VulkanResources
    {
        bool                           Initialized = false;
        VkDevice                       Device      = VK_NULL_HANDLE;
        Ref<VulkanDescriptorSetLayout> EmitLayout;
        Ref<VulkanDescriptorSetLayout> SimulateLayout;
        VulkanComputePipelineHandle    EmitPipeline{};
        VulkanComputePipelineHandle    SimulatePipeline{};
        Ref<VulkanDescriptorPool>      Pool;
    };
#else
    struct FluidSystemGPU::VulkanResources
    {
        // Empty stub on non-Vulkan builds (header still references the type)
    };
#endif

    FluidSystemGPU::FluidSystemGPU(uint32_t particleCount)
        : m_ParticleCount(particleCount), m_CudaImpl(CreateScope<CudaImpl>()),
          m_VulkanResources(CreateScope<VulkanResources>())
    {
    }

    FluidSystemGPU::~FluidSystemGPU()
    {
#ifdef ENGINE_ENABLE_VULKAN
        DestroyVulkanComputeResources();
#endif
        // CudaImpl 析构函数处理 CUDA 资源清理
    }

    void FluidSystemGPU::Init()
    {
        if (m_Initialized)
            return;

        ENGINE_INFO("[Fluid] Init() called, particleCount={}", m_ParticleCount);

        // Fluid-specific shaders
        m_EmitShader     = Shader::Create("assets/shaders/fluid_emit.glsl");
        m_SimulateShader = Shader::Create("assets/shaders/fluid_simulate.glsl");

        // Reuse existing SPH shaders
        m_SPHShaders = SPHShaderSet::Load();

        // Particle buffer: zero-initialized, 80 bytes per particle
        uint32_t             totalBytes = m_ParticleCount * sizeof(FluidGPUParticle);
        std::vector<uint8_t> zeroData(totalBytes, 0);
        m_ParticleBuffer = ShaderStorageBuffer::CreateGPUDynamic(zeroData.data(), totalBytes, 0);

        // Identity alive list: [0, 1, 2, ..., N-1]
        std::vector<uint32_t> aliveIndices(m_ParticleCount);
        for (uint32_t i = 0; i < m_ParticleCount; i++)
            aliveIndices[i] = i;
        m_AliveList = ShaderStorageBuffer::CreateGPUDynamic(aliveIndices.data(), m_ParticleCount * sizeof(uint32_t), 2);

        // Empty VAO for instanced draw
        m_EmptyVAO = VertexArray::Create();

#ifdef ENGINE_ENABLE_VULKAN
        // Vulkan 路径需要 UBO 上传 emitter / simulate 大块参数（仅 Vulkan 路径创建，
        // 避免 OpenGL 路径多余资源开销）。
        // MeshSDFMeta 异步回读 (Commit D 预埋；当前 SPH 段在 Vulkan 下跳过，CopyFrom 实际未触发)。
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
        {
            m_EmitParamsUBO = UniformBuffer::Create(sizeof(FluidEmitParamsUBO), FLUID_EMIT_UBO_BINDING);
            m_SimParamsUBO  = UniformBuffer::Create(sizeof(FluidSimParamsUBO), FLUID_SIM_UBO_BINDING);

            // SDF metadata 异步回读 ring。容量按 MeshSDFMeta 上限分配。
            m_SDFMetaReadback = GPUAsyncReadback::Create(MAX_MESH_SDF_BODIES * sizeof(GPUMeshSDFData));
        }
#endif

        m_Initialized = true;
    }

    void FluidSystemGPU::InitSPH(float smoothingRadius)
    {
        if (m_SPHInitialized)
            return;

        float    cellSize = 2.0f * smoothingRadius;
        uint32_t gridSize = 64;
        m_Grid.Init(m_ParticleCount, gridSize, cellSize);

        // Akinci 表面法线 SSBO: vec4 per particle, binding 8
        m_SurfaceNormalBuffer = ShaderStorageBuffer::CreateGPUOnly(m_ParticleCount * sizeof(glm::vec4), 8);

        m_SPHInitialized = true;
    }

    void FluidSystemGPU::InitPCISPH()
    {
        if (m_PCISPHInitialized)
            return;
        m_PCISPHBuffer      = ShaderStorageBuffer::CreateGPUOnly(m_ParticleCount * 48, 1);
        m_PCISPHInitialized = true;
    }

    void FluidSystemGPU::InitRigidBodyBuffer()
    {
        if (m_RigidBodyBuffer)
            return;
        m_RigidBodyBuffer = ShaderStorageBuffer::Create(MAX_RIGID_BODIES * sizeof(GPURigidBodyData), 3);
    }

    void FluidSystemGPU::InitMeshSDFBuffer()
    {
        if (m_MeshSDFMetaBuffer && m_MeshSDFVoxelBuffer)
            return;
        m_MeshSDFMetaBuffer  = ShaderStorageBuffer::Create(MAX_MESH_SDF_BODIES * sizeof(GPUMeshSDFData), 10);
        m_MeshSDFVoxelBuffer = ShaderStorageBuffer::Create(MAX_MESH_SDF_VOXELS * sizeof(float), 11);
    }

    void FluidSystemGPU::Emit(const glm::vec3& emitterPos, const FluidEmitterComponent& emitter)
    {
        if (!m_Initialized)
            return;

        m_ParticleBuffer->Bind(0);

        m_EmitShader->Bind();
        m_EmitShader->SetFloat3("u_EmitterPos", emitterPos);
        m_EmitShader->SetFloat3("u_EmitExtents", emitter.EmitExtents);
        m_EmitShader->SetFloat3("u_InitialVelocity", emitter.InitialVelocity);
        m_EmitShader->SetInt("u_ParticleCount", static_cast<int>(m_ParticleCount));
        m_EmitShader->SetFloat("u_Time", m_TotalTime);

        uint32_t groups = (m_ParticleCount + 63) / 64;
        RenderCommand::DispatchCompute(groups);
        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);
    }

    void FluidSystemGPU::Update(float                        dt,
                                const glm::vec3&             emitterPos,
                                const FluidEmitterComponent& emitter,
                                entt::registry*              registry)
    {
        if (!m_Initialized)
            return;

#ifdef ENGINE_ENABLE_VULKAN
        // Vulkan 路径分派 — emit + simulate 已迁，SPH 段（含 PCISPH 8 迭代）暂跳过，
        // 由 Commit C 接通。
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
        {
            UpdateVulkan(dt, emitterPos, emitter, registry);
            return;
        }
#endif

        float clampedDt = std::min(dt, 0.05f);
        m_TotalTime += dt;

        // Lazy init SPH grid
        if (!m_SPHInitialized)
            InitSPH(emitter.SmoothingRadius);

        // CPU-side kernel constant precomputation
        SPHKernelParams kp = SPHKernelParams::Compute(emitter.SmoothingRadius);

        // ---- GL Compute 路径 ----
        PerformanceMonitor::Get().GetFluidComputeGPUTimer().Begin();

        // Bind particle + alive list
        m_ParticleBuffer->Bind(0);
        m_AliveList->Bind(2);

        // --- SPH Pipeline ---
        float cellSize = m_Grid.GetCellSize();
        int   gridSize = static_cast<int>(m_Grid.GetGridSize());

        // Build spatial hash grid
        m_Grid.Build(m_ParticleCount);

        // SPH Density
        m_SurfaceNormalBuffer->Bind(8);
        m_SPHShaders.DensityShader->Bind();
        m_SPHShaders.DensityShader->SetInt("u_AliveCount", static_cast<int>(m_ParticleCount));
        m_SPHShaders.DensityShader->SetFloat("u_SmoothingRadius", kp.h);
        m_SPHShaders.DensityShader->SetFloat("u_ParticleMass", emitter.ParticleMass);
        m_SPHShaders.DensityShader->SetFloat("u_RestDensity", emitter.RestDensity);
        m_SPHShaders.DensityShader->SetFloat("u_GasConstant", emitter.GasConstant);
        m_SPHShaders.DensityShader->SetInt("u_GridSize", gridSize);
        m_SPHShaders.DensityShader->SetFloat("u_CellSize", cellSize);
        m_SPHShaders.DensityShader->SetFloat("u_Poly6Coeff", kp.poly6Coeff);
        m_SPHShaders.DensityShader->SetFloat("u_SpikyCoeff", kp.spikyCoeff);
        m_SPHShaders.DensityShader->SetFloat("u_SurfaceTension", emitter.SurfaceTension);

        uint32_t sphGroups = (m_ParticleCount + 255) / 256;
        RenderCommand::DispatchCompute(sphGroups);
        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

        if (emitter.PCISPHEnabled)
        {
            // --- PCISPH path ---
            InitPCISPH();

            uint32_t rigidBodyCount = 0;
            uint32_t meshSDFCount   = 0;
            uint32_t meshSDFVoxels  = 0;
            float    meshSDFBuildMs = 0.0f;
            if (emitter.RigidBodyCoupling && registry)
            {
                InitRigidBodyBuffer();
                rigidBodyCount = UploadRigidBodiesToBuffer(registry, m_RigidBodyBuffer, MAX_RIGID_BODIES,
                                                           RigidBodyUploadFilter::AllColliders);

                if (emitter.MeshSDFCoupling)
                {
                    InitMeshSDFBuffer();
                    auto                      uploadStart = std::chrono::high_resolution_clock::now();
                    const MeshSDFUploadResult upload      = UploadMeshSDFToBuffers(
                        registry, m_MeshSDFMetaBuffer, m_MeshSDFVoxelBuffer, MAX_MESH_SDF_BODIES,
                        static_cast<uint32_t>(std::max(emitter.MeshSDFResolution, 1)), emitter.MeshSDFBand,
                        emitter.MeshSDFBlend, RigidBodyUploadFilter::AllColliders);
                    auto uploadEnd = std::chrono::high_resolution_clock::now();
                    meshSDFCount   = upload.BodyCount;
                    meshSDFVoxels  = upload.VoxelCount;
                    meshSDFBuildMs = std::chrono::duration<float, std::milli>(uploadEnd - uploadStart).count();
                }
            }

            m_MeshSDFDebugBodies.clear();
            if (meshSDFCount > 0 && m_MeshSDFMetaBuffer)
            {
                std::vector<GPUMeshSDFData> metaReadback(meshSDFCount);
                m_MeshSDFMetaBuffer->GetData(metaReadback.data(),
                                             static_cast<uint32_t>(meshSDFCount * sizeof(GPUMeshSDFData)), 0);
                m_MeshSDFDebugBodies.reserve(meshSDFCount);
                for (const auto& meta : metaReadback)
                {
                    MeshSDFDebugBody dbg{};
                    dbg.Center                  = glm::vec3(meta.posAndType);
                    dbg.Rotation                = glm::eulerAngles(glm::quat_cast(
                        glm::mat3(glm::vec3(meta.rotCol0), glm::vec3(meta.rotCol1), glm::vec3(meta.rotCol2))));
                    const glm::vec3 invScale    = glm::vec3(meta.invScaleAndBlend);
                    const glm::vec3 scale       = glm::max(glm::abs(1.0f / invScale), glm::vec3(1e-4f));
                    const glm::vec3 worldExtent = glm::vec3(meta.localExtent) * scale;
                    dbg.HalfExtents             = 0.5f * worldExtent;
                    dbg.Resolution              = static_cast<uint32_t>(std::max(meta.gridParams.x, 0.0f));
                    dbg.VoxelCount              = static_cast<uint32_t>(std::max(meta.gridParams.z, 0.0f));
                    dbg.Band                    = meta.gridParams.w;
                    dbg.Blend                   = meta.invScaleAndBlend.w;
                    m_MeshSDFDebugBodies.push_back(dbg);
                }
            }

            m_MeshSDFDebugStats.Enabled          = emitter.MeshSDFCoupling;
            m_MeshSDFDebugStats.BodyCount        = meshSDFCount;
            m_MeshSDFDebugStats.VoxelCount       = meshSDFVoxels;
            m_MeshSDFDebugStats.EstimatedSamples = meshSDFCount * m_ParticleCount;
            m_MeshSDFDebugStats.Resolution       = static_cast<uint32_t>(std::max(emitter.MeshSDFResolution, 0));
            m_MeshSDFDebugStats.Band             = emitter.MeshSDFBand;
            m_MeshSDFDebugStats.LastBuildCpuMs   = meshSDFBuildMs;

            m_PCISPHBuffer->Bind(1);
            if (m_RigidBodyBuffer)
                m_RigidBodyBuffer->Bind(3);
            if (m_MeshSDFMetaBuffer)
                m_MeshSDFMetaBuffer->Bind(10);
            if (m_MeshSDFVoxelBuffer)
                m_MeshSDFVoxelBuffer->Bind(11);
            m_SurfaceNormalBuffer->Bind(8); // Akinci 表面法线（density pass 已写入）

            // PCISPH Init
            m_SPHShaders.PCISPHInit->Bind();
            m_SPHShaders.PCISPHInit->SetInt("u_AliveCount", static_cast<int>(m_ParticleCount));
            m_SPHShaders.PCISPHInit->SetFloat("u_SmoothingRadius", kp.h);
            m_SPHShaders.PCISPHInit->SetFloat("u_ParticleMass", emitter.ParticleMass);
            m_SPHShaders.PCISPHInit->SetFloat("u_Viscosity", emitter.Viscosity);
            m_SPHShaders.PCISPHInit->SetFloat("u_DeltaTime", clampedDt);
            m_SPHShaders.PCISPHInit->SetInt("u_GridSize", gridSize);
            m_SPHShaders.PCISPHInit->SetFloat("u_CellSize", cellSize);
            m_SPHShaders.PCISPHInit->SetFloat3("u_Gravity", emitter.Gravity);
            m_SPHShaders.PCISPHInit->SetFloat("u_SurfaceTension", emitter.SurfaceTension);
            m_SPHShaders.PCISPHInit->SetFloat("u_SpikyCoeff", kp.spikyCoeff);
            RenderCommand::DispatchCompute(sphGroups);
            RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

            int iterations = std::clamp(emitter.PCISPHIterations, 1, 8);

            // 单帧内完成所有 PCISPH 迭代（Predict → Density → Force）
            // 自适应 grid 策略：迭代 0 复用原始 grid，迭代 1+ 用预测位置重建
            for (int iter = 0; iter < iterations; iter++)
            {
                // 迭代 1+：用预测位置重建空间哈希 grid
                if (iter > 0)
                {
                    m_Grid.Build(m_ParticleCount, true);
                    // Grid.Build() 将 CellHash 绑定到 slot 1，覆盖了 PCISPHBuffer
                    // 必须恢复，否则后续 PCISPH shader（Predict/Density/Force）读不到正确的 PCISPHData
                    m_PCISPHBuffer->Bind(1);
                }

                // Predict: x* = pos + dt * v*
                m_SPHShaders.PCISPHPredict->Bind();
                m_SPHShaders.PCISPHPredict->SetInt("u_AliveCount", static_cast<int>(m_ParticleCount));
                m_SPHShaders.PCISPHPredict->SetFloat("u_DeltaTime", clampedDt);
                RenderCommand::DispatchCompute(sphGroups);
                RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                // Density: 在预测位置上计算密度和压力
                m_SPHShaders.PCISPHDensity->Bind();
                m_SPHShaders.PCISPHDensity->SetInt("u_AliveCount", static_cast<int>(m_ParticleCount));
                m_SPHShaders.PCISPHDensity->SetFloat("u_SmoothingRadius", kp.h);
                m_SPHShaders.PCISPHDensity->SetFloat("u_ParticleMass", emitter.ParticleMass);
                m_SPHShaders.PCISPHDensity->SetFloat("u_RestDensity", emitter.RestDensity);
                m_SPHShaders.PCISPHDensity->SetFloat(
                    "u_PCISPHDelta", SPHKernelMath::ComputePCISPHDelta(emitter.SmoothingRadius, emitter.ParticleMass,
                                                                       emitter.RestDensity, clampedDt));
                m_SPHShaders.PCISPHDensity->SetInt("u_GridSize", gridSize);
                m_SPHShaders.PCISPHDensity->SetFloat("u_CellSize", cellSize);
                m_SPHShaders.PCISPHDensity->SetFloat("u_Poly6Coeff", kp.poly6Coeff);
                m_SPHShaders.PCISPHDensity->SetInt("u_UsePredictedPos", (iter > 0) ? 1 : 0);
                RenderCommand::DispatchCompute(sphGroups);
                RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                // Force: 压力梯度力 + 刚体边界力 → v* += a_pressure * dt
                m_SPHShaders.PCISPHForce->Bind();
                m_SPHShaders.PCISPHForce->SetInt("u_AliveCount", static_cast<int>(m_ParticleCount));
                m_SPHShaders.PCISPHForce->SetFloat("u_SmoothingRadius", kp.h);
                m_SPHShaders.PCISPHForce->SetFloat("u_ParticleMass", emitter.ParticleMass);
                m_SPHShaders.PCISPHForce->SetFloat("u_DeltaTime", clampedDt);
                m_SPHShaders.PCISPHForce->SetInt("u_GridSize", gridSize);
                m_SPHShaders.PCISPHForce->SetFloat("u_CellSize", cellSize);
                m_SPHShaders.PCISPHForce->SetInt("u_RigidBodyCount", static_cast<int>(rigidBodyCount));
                m_SPHShaders.PCISPHForce->SetInt("u_MeshSDFCount", static_cast<int>(meshSDFCount));
                m_SPHShaders.PCISPHForce->SetInt("u_MeshSDFVoxelCount", static_cast<int>(meshSDFVoxels));
                m_SPHShaders.PCISPHForce->SetFloat("u_BoundaryStiffness", emitter.BoundaryStiffness);
                m_SPHShaders.PCISPHForce->SetFloat("u_BoundaryDamping", emitter.BoundaryDamping);
                m_SPHShaders.PCISPHForce->SetFloat("u_SpikyCoeff", kp.spikyCoeff);
                m_SPHShaders.PCISPHForce->SetInt("u_UsePredictedPos", (iter > 0) ? 1 : 0);
                RenderCommand::DispatchCompute(sphGroups);
                RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);
            }

            // Apply: 将最终预测速度写回粒子（每帧都执行）
            m_SPHShaders.PCISPHApply->Bind();
            m_SPHShaders.PCISPHApply->SetInt("u_AliveCount", static_cast<int>(m_ParticleCount));
            RenderCommand::DispatchCompute(sphGroups);
            RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);
        }
        else
        {
            // --- WCSPH path ---
            m_SurfaceNormalBuffer->Bind(8); // Akinci 表面法线（density pass 已写入）
            m_SPHShaders.ForceShader->Bind();
            m_SPHShaders.ForceShader->SetInt("u_AliveCount", static_cast<int>(m_ParticleCount));
            m_SPHShaders.ForceShader->SetFloat("u_SmoothingRadius", kp.h);
            m_SPHShaders.ForceShader->SetFloat("u_ParticleMass", emitter.ParticleMass);
            m_SPHShaders.ForceShader->SetFloat("u_Viscosity", emitter.Viscosity);
            m_SPHShaders.ForceShader->SetFloat("u_DeltaTime", clampedDt);
            m_SPHShaders.ForceShader->SetInt("u_GridSize", gridSize);
            m_SPHShaders.ForceShader->SetFloat("u_CellSize", cellSize);
            m_SPHShaders.ForceShader->SetFloat("u_SurfaceTension", emitter.SurfaceTension);
            m_SPHShaders.ForceShader->SetFloat("u_SpikyCoeff", kp.spikyCoeff);

            uint32_t rigidBodyCount = 0;
            uint32_t meshSDFCount   = 0;
            uint32_t meshSDFVoxels  = 0;
            float    meshSDFBuildMs = 0.0f;
            if (emitter.RigidBodyCoupling && registry)
            {
                InitRigidBodyBuffer();
                rigidBodyCount = UploadRigidBodiesToBuffer(registry, m_RigidBodyBuffer, MAX_RIGID_BODIES,
                                                           RigidBodyUploadFilter::AllColliders);
                m_RigidBodyBuffer->Bind(3);

                if (emitter.MeshSDFCoupling)
                {
                    InitMeshSDFBuffer();
                    auto                      uploadStart = std::chrono::high_resolution_clock::now();
                    const MeshSDFUploadResult upload      = UploadMeshSDFToBuffers(
                        registry, m_MeshSDFMetaBuffer, m_MeshSDFVoxelBuffer, MAX_MESH_SDF_BODIES,
                        static_cast<uint32_t>(std::max(emitter.MeshSDFResolution, 1)), emitter.MeshSDFBand,
                        emitter.MeshSDFBlend, RigidBodyUploadFilter::AllColliders);
                    auto uploadEnd = std::chrono::high_resolution_clock::now();
                    meshSDFCount   = upload.BodyCount;
                    meshSDFVoxels  = upload.VoxelCount;
                    meshSDFBuildMs = std::chrono::duration<float, std::milli>(uploadEnd - uploadStart).count();
                    m_MeshSDFMetaBuffer->Bind(10);
                    m_MeshSDFVoxelBuffer->Bind(11);
                }
            }

            m_MeshSDFDebugBodies.clear();
            if (meshSDFCount > 0 && m_MeshSDFMetaBuffer)
            {
                std::vector<GPUMeshSDFData> metaReadback(meshSDFCount);
                m_MeshSDFMetaBuffer->GetData(metaReadback.data(),
                                             static_cast<uint32_t>(meshSDFCount * sizeof(GPUMeshSDFData)), 0);
                m_MeshSDFDebugBodies.reserve(meshSDFCount);
                for (const auto& meta : metaReadback)
                {
                    MeshSDFDebugBody dbg{};
                    dbg.Center                  = glm::vec3(meta.posAndType);
                    dbg.Rotation                = glm::eulerAngles(glm::quat_cast(
                        glm::mat3(glm::vec3(meta.rotCol0), glm::vec3(meta.rotCol1), glm::vec3(meta.rotCol2))));
                    const glm::vec3 invScale    = glm::vec3(meta.invScaleAndBlend);
                    const glm::vec3 scale       = glm::max(glm::abs(1.0f / invScale), glm::vec3(1e-4f));
                    const glm::vec3 worldExtent = glm::vec3(meta.localExtent) * scale;
                    dbg.HalfExtents             = 0.5f * worldExtent;
                    dbg.Resolution              = static_cast<uint32_t>(std::max(meta.gridParams.x, 0.0f));
                    dbg.VoxelCount              = static_cast<uint32_t>(std::max(meta.gridParams.z, 0.0f));
                    dbg.Band                    = meta.gridParams.w;
                    dbg.Blend                   = meta.invScaleAndBlend.w;
                    m_MeshSDFDebugBodies.push_back(dbg);
                }
            }

            m_MeshSDFDebugStats.Enabled          = emitter.MeshSDFCoupling;
            m_MeshSDFDebugStats.BodyCount        = meshSDFCount;
            m_MeshSDFDebugStats.VoxelCount       = meshSDFVoxels;
            m_MeshSDFDebugStats.EstimatedSamples = meshSDFCount * m_ParticleCount;
            m_MeshSDFDebugStats.Resolution       = static_cast<uint32_t>(std::max(emitter.MeshSDFResolution, 0));
            m_MeshSDFDebugStats.Band             = emitter.MeshSDFBand;
            m_MeshSDFDebugStats.LastBuildCpuMs   = meshSDFBuildMs;
            m_SPHShaders.ForceShader->SetInt("u_RigidBodyCount", static_cast<int>(rigidBodyCount));
            m_SPHShaders.ForceShader->SetInt("u_MeshSDFCount", static_cast<int>(meshSDFCount));
            m_SPHShaders.ForceShader->SetInt("u_MeshSDFVoxelCount", static_cast<int>(meshSDFVoxels));
            m_SPHShaders.ForceShader->SetFloat("u_BoundaryStiffness", emitter.BoundaryStiffness);
            m_SPHShaders.ForceShader->SetFloat("u_BoundaryDamping", emitter.BoundaryDamping);

            RenderCommand::DispatchCompute(sphGroups);
            RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);
        }

        // Rebind particle + alive buffers (Grid passes reuse some binding slots)
        m_ParticleBuffer->Bind(0);
        m_AliveList->Bind(2);

        // --- Simulate: integrate position/velocity + boundary ---
        glm::vec3 simGravity = emitter.PCISPHEnabled ? glm::vec3(0.0f) : emitter.Gravity;

        m_SimulateShader->Bind();
        m_SimulateShader->SetFloat("u_DeltaTime", clampedDt);
        m_SimulateShader->SetFloat3("u_Gravity", simGravity);
        m_SimulateShader->SetFloat("u_Damping", emitter.Damping);
        m_SimulateShader->SetInt("u_ParticleCount", static_cast<int>(m_ParticleCount));
        m_SimulateShader->SetFloat3("u_BoundaryMin", emitterPos + emitter.BoundaryMin);
        m_SimulateShader->SetFloat3("u_BoundaryMax", emitterPos + emitter.BoundaryMax);
        m_SimulateShader->SetInt("u_UseBoundary", emitter.UseBoundary ? 1 : 0);
        m_SimulateShader->SetInt("u_PCISPHMode", emitter.PCISPHEnabled ? 1 : 0);

        uint32_t simGroups = (m_ParticleCount + 255) / 256;
        RenderCommand::DispatchCompute(simGroups);
        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

        PerformanceMonitor::Get().GetFluidComputeGPUTimer().End();

        PerformanceMonitor::Get().SetFluidActive(true);
    }

#ifdef ENGINE_ENABLE_VULKAN
    // =========================================================================
    // Vulkan 路径实现（Commit D）
    //
    // 范围：fluid_emit + fluid_simulate 录入主帧 cmd。
    // SPH 段（密度 / 力 / PCISPH 8 迭代）在 Vulkan 路径下显式 WARN+跳过，
    // 等 Commit C 补齐 sph_*.glsl 的 #ifdef VULKAN 分支后接通。
    //
    // 见 SPEC §3 D-3 / D-9 / D-12，ADR-0002。
    // =========================================================================

    bool FluidSystemGPU::InitVulkanComputeResources()
    {
        if (m_VulkanResources->Initialized)
            return true;

        auto* ctx = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(ctx != nullptr, "[Fluid][Vulkan] VulkanContext not initialized");
        m_VulkanResources->Device = ctx->GetDevice();

        auto emitVk     = std::dynamic_pointer_cast<VulkanShader>(m_EmitShader);
        auto simulateVk = std::dynamic_pointer_cast<VulkanShader>(m_SimulateShader);
        ENGINE_CORE_RELEASE_ASSERT(emitVk && simulateVk, "[Fluid][Vulkan] fluid shader 转型失败（非 VulkanShader）");

        VkShaderModule emitModule     = emitVk->GetOrCreateShaderModule(m_VulkanResources->Device, "compute");
        VkShaderModule simulateModule = simulateVk->GetOrCreateShaderModule(m_VulkanResources->Device, "compute");
        ENGINE_CORE_RELEASE_ASSERT(emitModule != VK_NULL_HANDLE && simulateModule != VK_NULL_HANDLE,
                                   "[Fluid][Vulkan] 无法创建 fluid compute shader module");

        // Descriptor set layouts —  由 SPIR-V 反射结果构建
        m_VulkanResources->EmitLayout = VulkanDescriptorSetLayout::CreateFromReflection(
            m_VulkanResources->Device, emitVk->GetReflectedBindings(), 0);
        m_VulkanResources->SimulateLayout = VulkanDescriptorSetLayout::CreateFromReflection(
            m_VulkanResources->Device, simulateVk->GetReflectedBindings(), 0);

        // Compute pipelines
        auto buildPipeline = [&](const Ref<VulkanShader>& shader, VkShaderModule module,
                                 const Ref<VulkanDescriptorSetLayout>& setLayout) -> VulkanComputePipelineHandle
        {
            VulkanComputePipelineDesc desc{};
            desc.ShaderModule = module;
            desc.EntryPoint   = "main";
            desc.SetLayouts   = {setLayout->GetHandle()};
            for (const auto& pc : shader->GetReflectedPushConstants())
            {
                VkPushConstantRange r{};
                r.offset     = pc.Offset;
                r.size       = pc.Size;
                r.stageFlags = pc.Stages;
                desc.PushConstants.push_back(r);
            }
            return VulkanPipeline::CreateCompute(m_VulkanResources->Device, desc);
        };

        m_VulkanResources->EmitPipeline = buildPipeline(emitVk, emitModule, m_VulkanResources->EmitLayout);
        m_VulkanResources->SimulatePipeline =
            buildPipeline(simulateVk, simulateModule, m_VulkanResources->SimulateLayout);

        // 每帧最多 2 sets（emit + simulate）；64 容量给充足余量。
        m_VulkanResources->Pool = VulkanDescriptorPool::CreateDefaultComputePool(m_VulkanResources->Device, 64);

        m_VulkanResources->Initialized = true;
        ENGINE_CORE_INFO("[Fluid][Vulkan] 流体 compute pipeline 初始化完成 (emit + simulate)");
        return true;
    }

    void FluidSystemGPU::DestroyVulkanComputeResources()
    {
        if (!m_VulkanResources || !m_VulkanResources->Initialized)
            return;

        if (m_VulkanResources->Device != VK_NULL_HANDLE)
            vkDeviceWaitIdle(m_VulkanResources->Device);

        VulkanPipeline::DestroyCompute(m_VulkanResources->Device, m_VulkanResources->EmitPipeline);
        VulkanPipeline::DestroyCompute(m_VulkanResources->Device, m_VulkanResources->SimulatePipeline);
        m_VulkanResources->Pool.reset();
        m_VulkanResources->EmitLayout.reset();
        m_VulkanResources->SimulateLayout.reset();
        m_VulkanResources->Device      = VK_NULL_HANDLE;
        m_VulkanResources->Initialized = false;
    }

    void FluidSystemGPU::UpdateVulkan(float                        dt,
                                      const glm::vec3&             emitterPos,
                                      const FluidEmitterComponent& emitter,
                                      entt::registry*              registry)
    {
        (void)registry; // SPH 段在 Vulkan 路径下跳过，不需要 registry

        auto* ctx = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(ctx != nullptr, "[Fluid][Vulkan] VulkanContext not initialized");

        VkCommandBuffer cmd = ctx->GetCurrentFrameCommandBuffer();
        if (cmd == VK_NULL_HANDLE)
        {
            // BeginFrame 未执行（swapchain recreate 或 SmokeLayer 直走 SwapBuffers），跳过录制
            return;
        }

        if (!InitVulkanComputeResources())
            return;

        const float clampedDt = std::min(dt, 0.05f);
        m_TotalTime += dt;

        // Lazy init SPH grid（实际 Vulkan 路径 SPH 段当前跳过，但保持现状以便 Commit C 接通）
        if (!m_SPHInitialized)
            InitSPH(emitter.SmoothingRadius);

        PerformanceMonitor::Get().GetFluidComputeGPUTimer().Begin();

        // ---------------- fluid_emit dispatch ----------------
        // 流体粒子是一次性发射，每帧都重新初始化（与 OpenGL 路径 Emit() 同语义，但这里没单独
        // Emit 调用入口，按当前实现 simulate 之外不调度 emit；Vulkan 路径下亦如此，
        // 维持与原 OpenGL Update 行为一致）。
        //
        // 不过为遵循"FluidSystemGPU 全 dispatch Vulkan 化"目标，本路径下若上层调用 Emit()
        // 会经 RenderCommand::DispatchCompute（OpenGL 路径），Vulkan 路径下 Emit() 当前
        // 不会被外部触发。完整 emit 接入路径在 Commit C 接通 SceneRenderer 后明确。
        //
        // 本 Update 录制只走 simulate（与原 OpenGL Update 行为对齐：Update 内不调 emit）。
        //
        // [SPH 段跳过] ----------------
        {
            static bool s_SPHSkipWarned = false;
            if (!s_SPHSkipWarned)
            {
                ENGINE_CORE_WARN("[Vulkan] FluidSystemGPU SPH path not yet migrated (Commit C). "
                                 "Density / Force / PCISPH 迭代跳过，仅执行 fluid_simulate。");
                s_SPHSkipWarned = true;
            }
        }

        // ---------------- fluid_simulate dispatch ----------------
        // 写 simulate UBO（gravity + damping + boundary + mode）
        FluidSimParamsUBO simUbo{};
        glm::vec3         simGravity = emitter.PCISPHEnabled ? glm::vec3(0.0f) : emitter.Gravity;
        simUbo.GravityAndDamping     = glm::vec4(simGravity, emitter.Damping);
        simUbo.BoundaryMinAndUseFlag = glm::vec4(emitterPos + emitter.BoundaryMin, emitter.UseBoundary ? 1.0f : 0.0f);
        simUbo.BoundaryMaxAndMode    = glm::vec4(emitterPos + emitter.BoundaryMax, emitter.PCISPHEnabled ? 1.0f : 0.0f);
        m_SimParamsUBO->SetData(&simUbo, sizeof(FluidSimParamsUBO));

        VkDescriptorSet simulateSet = m_VulkanResources->Pool->Allocate(m_VulkanResources->SimulateLayout->GetHandle());
        if (simulateSet == VK_NULL_HANDLE)
        {
            ENGINE_CORE_WARN("[Fluid][Vulkan] DescriptorPool 耗尽，Reset 后重试");
            m_VulkanResources->Pool->Reset();
            simulateSet = m_VulkanResources->Pool->Allocate(m_VulkanResources->SimulateLayout->GetHandle());
        }
        ENGINE_CORE_RELEASE_ASSERT(simulateSet != VK_NULL_HANDLE, "[Fluid][Vulkan] simulate DescriptorPool 分配仍失败");

        auto vkParticle = std::dynamic_pointer_cast<VulkanStorageBuffer>(m_ParticleBuffer);
        auto vkSimUBO   = std::dynamic_pointer_cast<VulkanUniformBuffer>(m_SimParamsUBO);
        ENGINE_CORE_RELEASE_ASSERT(vkParticle && vkSimUBO, "[Fluid][Vulkan] particle/sim UBO 转型失败");

        {
            VulkanDescriptorWriter w;
            w.WriteBuffer(0, vkParticle->GetBuffer(), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(FLUID_SIM_UBO_BINDING, vkSimUBO->GetBuffer(), 0, sizeof(FluidSimParamsUBO),
                          VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
            w.UpdateSet(m_VulkanResources->Device, simulateSet);
        }

        VulkanCommandBuffer cmdBuf(cmd);
        cmdBuf.BindComputePipeline(m_VulkanResources->SimulatePipeline.Pipeline);
        cmdBuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, m_VulkanResources->SimulatePipeline.Layout, 0,
                                  {simulateSet});

        FluidSimulatePC simPC{};
        simPC.DeltaTime     = clampedDt;
        simPC.ParticleCount = m_ParticleCount;
        cmdBuf.PushConstants(m_VulkanResources->SimulatePipeline.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                             sizeof(FluidSimulatePC), &simPC);

        const uint32_t simGroups = (m_ParticleCount + 255) / 256;
        if (simGroups > 0)
            cmdBuf.Dispatch(simGroups, 1, 1);

        {
            auto m = ResolveBarrierBits(BarrierBit::ShaderStorage);
            cmdBuf.MemoryBarrier(m.SrcStage, m.DstStage, m.SrcAccess, m.DstAccess);
        }

        // ---------------- MeshSDFMeta 异步回读（Commit D 预埋）----------------
        // SPH 段在 Vulkan 下跳过，m_MeshSDFMetaBuffer 当前不会被 InitMeshSDFBuffer 创建，
        // 此 guard 让 Commit C 接通 SPH 段后该路径自动生效。
        if (m_SDFMetaReadback && m_MeshSDFMetaBuffer)
        {
            if (m_SDFMetaReadback->IsPending() && m_SDFMetaReadback->IsReady())
            {
                // 读最老槽（3 帧延迟）。当前为 placeholder（实际消费方留待 Commit C 落地）。
                std::vector<GPUMeshSDFData> staging(MAX_MESH_SDF_BODIES);
                m_SDFMetaReadback->GetData(staging.data(), MAX_MESH_SDF_BODIES * sizeof(GPUMeshSDFData));
                // 注：Commit C 接通 SPH 后会用 staging 填充 m_MeshSDFDebugBodies，
                // 当前 Vulkan 路径 SPH 段跳过，此处仅完成读取消费 ring 槽避免堆积。
            }
            m_SDFMetaReadback->CopyFrom(m_MeshSDFMetaBuffer, MAX_MESH_SDF_BODIES * sizeof(GPUMeshSDFData), 0);
        }

        PerformanceMonitor::Get().GetFluidComputeGPUTimer().End();
        PerformanceMonitor::Get().SetFluidActive(true);
    }
#endif // ENGINE_ENABLE_VULKAN

} // namespace Engine
