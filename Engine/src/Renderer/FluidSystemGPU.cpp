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
#include <glad/gl.h>

#include "Debug/PerformanceMonitor.h"

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

    FluidSystemGPU::FluidSystemGPU(uint32_t particleCount)
        : m_ParticleCount(particleCount), m_CudaImpl(CreateScope<CudaImpl>())
    {
    }

    FluidSystemGPU::~FluidSystemGPU()
    {
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
        m_EmitShader->SetFloat("u_Time", 0.0f);

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
                    m_PCISPHBuffer->Bind(9); // binding 9: PCISPHData for predicted pos
                    m_Grid.Build(m_ParticleCount, true);
                    // 重新绑定 PCISPH 使用的 buffer slots
                    m_ParticleBuffer->Bind(0);
                    m_AliveList->Bind(2);
                    m_PCISPHBuffer->Bind(1);
                    if (m_RigidBodyBuffer)
                        m_RigidBodyBuffer->Bind(3);
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

} // namespace Engine
