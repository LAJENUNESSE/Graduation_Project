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

    // MeshCollider Transform 哈希：用于检测碰撞体是否移动
    static size_t ComputeMeshColliderHash(entt::registry* registry)
    {
        if (!registry)
            return 0;

        size_t hash = 0;
        auto   view = registry->view<TransformComponent, MeshColliderComponent>();
        for (auto entity : view)
        {
            auto& tc = view.get<TransformComponent>(entity);
            // 简单组合 Translation + Rotation + Scale 的 bit pattern
            auto hashFloat = [](float f) -> size_t
            {
                uint32_t bits;
                std::memcpy(&bits, &f, sizeof(bits));
                return std::hash<uint32_t>{}(bits);
            };
            hash ^= hashFloat(tc.Translation.x) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= hashFloat(tc.Translation.y) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= hashFloat(tc.Translation.z) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= hashFloat(tc.Rotation.x) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= hashFloat(tc.Rotation.y) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= hashFloat(tc.Rotation.z) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= hashFloat(tc.Scale.x) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= hashFloat(tc.Scale.y) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= hashFloat(tc.Scale.z) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return hash;
    }

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
        m_CompactShader  = Shader::Create("assets/shaders/fluid_compact.glsl");

        // Reuse existing SPH shaders
        m_SPHShaders = SPHShaderSet::Load();

        // Particle buffer: zero-initialized, 80 bytes per particle
        // posAndLife.w = 0 means dead (for lifetime mode all start dead)
        uint32_t             totalBytes = m_ParticleCount * sizeof(FluidGPUParticle);
        std::vector<uint8_t> zeroData(totalBytes, 0);
        m_ParticleBuffer = ShaderStorageBuffer::CreateGPUDynamic(zeroData.data(), totalBytes, 0);

        // Alive list (will be rebuilt by compact pass in lifetime mode)
        std::vector<uint32_t> identityIndices(m_ParticleCount);
        for (uint32_t i = 0; i < m_ParticleCount; i++)
            identityIndices[i] = i;
        m_AliveList =
            ShaderStorageBuffer::CreateGPUDynamic(identityIndices.data(), m_ParticleCount * sizeof(uint32_t), 2);

        // Dead list: initially all particles are dead (for lifetime mode)
        m_DeadList =
            ShaderStorageBuffer::CreateGPUDynamic(identityIndices.data(), m_ParticleCount * sizeof(uint32_t), 12);

        // Counter buffer: {deadCount, aliveCount, emitCount, pad}
        struct CounterData
        {
            uint32_t deadCount;
            uint32_t aliveCount;
            uint32_t emitCount;
            uint32_t pad;
        };
        CounterData initCounters{m_ParticleCount, 0, 0, 0};
        m_CounterBuffer = ShaderStorageBuffer::CreateGPUDynamic(&initCounters, sizeof(CounterData), 13);

        // Empty VAO for instanced draw
        m_EmptyVAO = VertexArray::Create();

        m_Initialized = true;
    }

    void FluidSystemGPU::InitSPH(float smoothingRadius)
    {
        if (m_SPHInitialized)
            return;

        float    cellSize = 2.0f * smoothingRadius;
        uint32_t gridSize = (m_ParticleCount <= 8000) ? 16 : (m_ParticleCount <= 30000) ? 32 : 64;
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

        const bool lifetimeMode = (emitter.EmitRate > 0.0f && emitter.ParticleLifetime > 0.0f);

        m_ParticleBuffer->Bind(0);
        m_DeadList->Bind(12);
        m_CounterBuffer->Bind(13);

        m_EmitShader->Bind();
        m_EmitShader->SetFloat3("u_EmitterPos", emitterPos);
        m_EmitShader->SetFloat3("u_EmitExtents", emitter.EmitExtents);
        m_EmitShader->SetFloat3("u_InitialVelocity", emitter.InitialVelocity);
        m_EmitShader->SetInt("u_ParticleCount", static_cast<int>(m_ParticleCount));
        m_EmitShader->SetFloat("u_Time", m_TotalTime);
        m_EmitShader->SetFloat("u_ParticleLifetime", emitter.ParticleLifetime);
        m_EmitShader->SetInt("u_UseLifetime", lifetimeMode ? 1 : 0);

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

        float clampedDt = std::min(dt, 0.008f);
        m_TotalTime += dt;

        const bool lifetimeMode = (emitter.EmitRate > 0.0f && emitter.ParticleLifetime > 0.0f);

        // Lazy init SPH grid
        if (!m_SPHInitialized)
            InitSPH(emitter.SmoothingRadius);

        // CPU-side kernel constant precomputation
        SPHKernelParams kp = SPHKernelParams::Compute(emitter.SmoothingRadius);

        // ---- Lifetime: emit rate accumulation + emit + compact ----
        if (lifetimeMode)
        {
            float emitDt = std::min(dt, 0.05f);
            m_EmitAccumulator += emitter.EmitRate * emitDt;
            uint32_t emitCount = static_cast<uint32_t>(m_EmitAccumulator);
            m_EmitAccumulator -= static_cast<float>(emitCount);
            emitCount = std::min(emitCount, m_ParticleCount);

            if (emitCount > 0)
            {
                // Write emitCount to counter buffer
                m_CounterBuffer->SetData(&emitCount, sizeof(uint32_t), 8); // offset 8 = emitCount field

                // Emit from dead list
                Emit(emitterPos, emitter);
            }

            // Compact pass: rebuild alive/dead lists for SPH
            struct CounterZero
            {
                uint32_t deadCount;
                uint32_t aliveCount;
                uint32_t emitCount;
                uint32_t pad;
            };
            CounterZero zero{0, 0, 0, 0};
            m_CounterBuffer->SetData(&zero, sizeof(CounterZero), 0);
            RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

            m_ParticleBuffer->Bind(0);
            m_AliveList->Bind(2);
            m_DeadList->Bind(12);
            m_CounterBuffer->Bind(13);

            m_CompactShader->Bind();
            m_CompactShader->SetInt("u_MaxParticles", static_cast<int>(m_ParticleCount));
            uint32_t compactGroups = (m_ParticleCount + 255) / 256;
            RenderCommand::DispatchCompute(compactGroups);
            RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

            // Read alive count from compact pass for this frame's SPH
            uint32_t aliveAfterCompact = 0;
            m_CounterBuffer->GetData(&aliveAfterCompact, sizeof(uint32_t), 4); // offset 4 = aliveCount
            m_LastAliveCount = aliveAfterCompact;
        }

        // ---- GL Compute 路径 ----
        PerformanceMonitor::Get().GetFluidComputeGPUTimer().Begin();

        // Bind particle + alive list
        m_ParticleBuffer->Bind(0);
        m_AliveList->Bind(2);

        // Use alive count for SPH dispatch
        uint32_t sphParticleCount = lifetimeMode ? std::max(m_LastAliveCount, 1u) : m_ParticleCount;

        // --- SPH Pipeline ---
        float cellSize = m_Grid.GetCellSize();
        int   gridSize = static_cast<int>(m_Grid.GetGridSize());

        // Build spatial hash grid
        m_Grid.Build(sphParticleCount);

        // SPH Density
        m_SurfaceNormalBuffer->Bind(8);
        m_SPHShaders.DensityShader->Bind();
        m_SPHShaders.DensityShader->SetInt("u_AliveCount", static_cast<int>(sphParticleCount));
        m_SPHShaders.DensityShader->SetFloat("u_SmoothingRadius", kp.h);
        m_SPHShaders.DensityShader->SetFloat("u_ParticleMass", emitter.ParticleMass);
        m_SPHShaders.DensityShader->SetFloat("u_RestDensity", emitter.RestDensity);
        m_SPHShaders.DensityShader->SetFloat("u_GasConstant", emitter.GasConstant);
        m_SPHShaders.DensityShader->SetInt("u_GridSize", gridSize);
        m_SPHShaders.DensityShader->SetFloat("u_CellSize", cellSize);
        m_SPHShaders.DensityShader->SetFloat("u_Poly6Coeff", kp.poly6Coeff);
        m_SPHShaders.DensityShader->SetFloat("u_SpikyCoeff", kp.spikyCoeff);
        m_SPHShaders.DensityShader->SetFloat("u_SurfaceTension", emitter.SurfaceTension);

        uint32_t sphGroups = (sphParticleCount + 255) / 256;
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
                    size_t currentHash = ComputeMeshColliderHash(registry);
                    if (!m_MeshSDFCacheValid || currentHash != m_MeshSDFCacheHash)
                    {
                        auto                      uploadStart = std::chrono::high_resolution_clock::now();
                        const MeshSDFUploadResult upload      = UploadMeshSDFToBuffers(
                            registry, m_MeshSDFMetaBuffer, m_MeshSDFVoxelBuffer, MAX_MESH_SDF_BODIES,
                            static_cast<uint32_t>(std::max(emitter.MeshSDFResolution, 1)), emitter.MeshSDFBand,
                            emitter.MeshSDFBlend, RigidBodyUploadFilter::AllColliders);
                        auto uploadEnd = std::chrono::high_resolution_clock::now();
                        meshSDFBuildMs = std::chrono::duration<float, std::milli>(uploadEnd - uploadStart).count();
                        m_CachedMeshSDFResult = upload;
                        m_MeshSDFCacheHash    = currentHash;
                        m_MeshSDFCacheValid   = true;
                    }
                    meshSDFCount  = m_CachedMeshSDFResult.BodyCount;
                    meshSDFVoxels = m_CachedMeshSDFResult.VoxelCount;
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
            m_SPHShaders.PCISPHInit->SetInt("u_AliveCount", static_cast<int>(sphParticleCount));
            m_SPHShaders.PCISPHInit->SetFloat("u_SmoothingRadius", kp.h);
            m_SPHShaders.PCISPHInit->SetFloat("u_ParticleMass", emitter.ParticleMass);
            m_SPHShaders.PCISPHInit->SetFloat("u_Viscosity", emitter.Viscosity);
            m_SPHShaders.PCISPHInit->SetFloat("u_DeltaTime", clampedDt);
            m_SPHShaders.PCISPHInit->SetInt("u_GridSize", gridSize);
            m_SPHShaders.PCISPHInit->SetFloat("u_CellSize", cellSize);
            m_SPHShaders.PCISPHInit->SetFloat3("u_Gravity", emitter.Gravity);
            m_SPHShaders.PCISPHInit->SetFloat("u_SurfaceTension", emitter.SurfaceTension);
            m_SPHShaders.PCISPHInit->SetFloat("u_SpikyCoeff", kp.spikyCoeff);
            m_SPHShaders.PCISPHInit->SetFloat("u_RestDensity", emitter.RestDensity);
            RenderCommand::DispatchCompute(sphGroups);
            RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

            int iterations = std::clamp(emitter.PCISPHIterations, 1, 8);

            // 单帧内完成所有 PCISPH 迭代（Predict → Density → Force）
            // 所有迭代复用初始 grid（性能优化：省去迭代 1+ 的网格重建开销）
            for (int iter = 0; iter < iterations; iter++)
            {
                // Predict: x* = pos + dt * v*
                m_SPHShaders.PCISPHPredict->Bind();
                m_SPHShaders.PCISPHPredict->SetInt("u_AliveCount", static_cast<int>(sphParticleCount));
                m_SPHShaders.PCISPHPredict->SetFloat("u_DeltaTime", clampedDt);
                RenderCommand::DispatchCompute(sphGroups);
                RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                // Density: 在预测位置上计算密度和压力
                m_SPHShaders.PCISPHDensity->Bind();
                m_SPHShaders.PCISPHDensity->SetInt("u_AliveCount", static_cast<int>(sphParticleCount));
                m_SPHShaders.PCISPHDensity->SetFloat("u_SmoothingRadius", kp.h);
                m_SPHShaders.PCISPHDensity->SetFloat("u_ParticleMass", emitter.ParticleMass);
                m_SPHShaders.PCISPHDensity->SetFloat("u_RestDensity", emitter.RestDensity);
                m_SPHShaders.PCISPHDensity->SetFloat(
                    "u_PCISPHDelta", SPHKernelMath::ComputePCISPHDelta(emitter.SmoothingRadius, emitter.ParticleMass,
                                                                       emitter.RestDensity, clampedDt));
                m_SPHShaders.PCISPHDensity->SetInt("u_GridSize", gridSize);
                m_SPHShaders.PCISPHDensity->SetFloat("u_CellSize", cellSize);
                m_SPHShaders.PCISPHDensity->SetFloat("u_Poly6Coeff", kp.poly6Coeff);
                m_SPHShaders.PCISPHDensity->SetInt("u_UsePredictedPos", 0);
                RenderCommand::DispatchCompute(sphGroups);
                RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                // Force: 压力梯度力 + 刚体边界力 → v* += a_pressure * dt
                m_SPHShaders.PCISPHForce->Bind();
                m_SPHShaders.PCISPHForce->SetInt("u_AliveCount", static_cast<int>(sphParticleCount));
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
                m_SPHShaders.PCISPHForce->SetInt("u_UsePredictedPos", 0);
                m_SPHShaders.PCISPHForce->SetFloat3("u_BoundaryMin", emitter.BoundaryMin + emitterPos);
                m_SPHShaders.PCISPHForce->SetFloat3("u_BoundaryMax", emitter.BoundaryMax + emitterPos);
                m_SPHShaders.PCISPHForce->SetInt("u_UseBoundary", emitter.UseBoundary ? 1 : 0);
                RenderCommand::DispatchCompute(sphGroups);
                RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);
            }

            // 最终 Predict: 用最终 v* 重新计算 x*，确保 x*-v* 一致
            m_SPHShaders.PCISPHPredict->Bind();
            m_SPHShaders.PCISPHPredict->SetInt("u_AliveCount", static_cast<int>(sphParticleCount));
            m_SPHShaders.PCISPHPredict->SetFloat("u_DeltaTime", clampedDt);
            RenderCommand::DispatchCompute(sphGroups);
            RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

            // Apply: 将最终预测速度写回粒子 + 刚体穿透硬约束
            m_SPHShaders.PCISPHApply->Bind();
            m_SPHShaders.PCISPHApply->SetInt("u_AliveCount", static_cast<int>(sphParticleCount));
            m_SPHShaders.PCISPHApply->SetFloat("u_MaxSpeed", kp.h / clampedDt);
            m_SPHShaders.PCISPHApply->SetInt("u_RigidBodyCount", static_cast<int>(rigidBodyCount));
            RenderCommand::DispatchCompute(sphGroups);
            RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);
        }
        else
        {
            // --- WCSPH path ---
            m_SurfaceNormalBuffer->Bind(8); // Akinci 表面法线（density pass 已写入）
            m_SPHShaders.ForceShader->Bind();
            m_SPHShaders.ForceShader->SetInt("u_AliveCount", static_cast<int>(sphParticleCount));
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
                    size_t currentHash = ComputeMeshColliderHash(registry);
                    if (!m_MeshSDFCacheValid || currentHash != m_MeshSDFCacheHash)
                    {
                        auto                      uploadStart = std::chrono::high_resolution_clock::now();
                        const MeshSDFUploadResult upload      = UploadMeshSDFToBuffers(
                            registry, m_MeshSDFMetaBuffer, m_MeshSDFVoxelBuffer, MAX_MESH_SDF_BODIES,
                            static_cast<uint32_t>(std::max(emitter.MeshSDFResolution, 1)), emitter.MeshSDFBand,
                            emitter.MeshSDFBlend, RigidBodyUploadFilter::AllColliders);
                        auto uploadEnd = std::chrono::high_resolution_clock::now();
                        meshSDFBuildMs = std::chrono::duration<float, std::milli>(uploadEnd - uploadStart).count();
                        m_CachedMeshSDFResult = upload;
                        m_MeshSDFCacheHash    = currentHash;
                        m_MeshSDFCacheValid   = true;
                    }
                    meshSDFCount  = m_CachedMeshSDFResult.BodyCount;
                    meshSDFVoxels = m_CachedMeshSDFResult.VoxelCount;
                    m_MeshSDFMetaBuffer->Bind(10);
                    m_MeshSDFVoxelBuffer->Bind(11);
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

        // --- Simulate: integrate position/velocity + boundary + lifetime ---
        if (lifetimeMode)
        {
            // Zero counters before simulate (simulate rebuilds alive/dead lists)
            struct CounterZero
            {
                uint32_t deadCount;
                uint32_t aliveCount;
                uint32_t emitCount;
                uint32_t pad;
            };
            CounterZero zero{0, 0, 0, 0};
            m_CounterBuffer->SetData(&zero, sizeof(CounterZero), 0);
            RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

            m_DeadList->Bind(12);
            m_CounterBuffer->Bind(13);
        }

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
        m_SimulateShader->SetFloat("u_MaxSpeed", kp.h / clampedDt);
        m_SimulateShader->SetInt("u_UseLifetime", lifetimeMode ? 1 : 0);

        uint32_t simGroups = (m_ParticleCount + 255) / 256;
        RenderCommand::DispatchCompute(simGroups);
        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

        // Lifetime: read back alive count for next frame's SPH dispatch
        if (lifetimeMode)
        {
            // Use GPU counter from simulate pass (1-frame delay is acceptable)
            uint32_t aliveCount = 0;
            m_CounterBuffer->GetData(&aliveCount, sizeof(uint32_t), 4); // offset 4 = aliveCount
            m_LastAliveCount = aliveCount;
        }

        PerformanceMonitor::Get().GetFluidComputeGPUTimer().End();

        PerformanceMonitor::Get().SetFluidActive(true);
    }

} // namespace Engine
