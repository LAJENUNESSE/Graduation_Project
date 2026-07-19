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
#include <cstring>

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

#ifdef ENGINE_ENABLE_CUDA
#include "Platform/CUDA/CudaGLInteropContext.h"
#include "Platform/CUDA/CudaSPHPipeline.h"
#include "Platform/CUDA/CudaParticlePipeline.h"
#include "Platform/CUDA/CudaParticleTypes.h"
#include "Platform/CUDA/CudaErrorHandling.h"
#include "Platform/CUDA/CudaTimingHelper.h"
#endif

namespace Engine
{

    // =========================================================================
    // CudaImpl stub (CUDA removed)
    // =========================================================================
#ifdef ENGINE_ENABLE_CUDA
    struct FluidSystemGPU::CudaImpl
    {
        Scope<CudaGLInteropContext> GLInterop;
        bool                        UseCudaPath   = false;
        bool                        InitAttempted = false;

        int   SlotParticle = -1;
        void* SPHCtx       = nullptr;

        CudaTimingHelper Timing;

        ~CudaImpl()
        {
            if (SPHCtx)
                CudaInterop::DestroySPHContext(SPHCtx);
            Timing.Destroy();
        }

        void* GetStream() const { return GLInterop ? GLInterop->GetStream() : nullptr; }
        void* GetMappedPointer(int slot) const { return GLInterop ? GLInterop->GetMappedPointer(slot) : nullptr; }
        bool  MapAll() { return GLInterop ? GLInterop->MapAll() : false; }
        void  UnmapAll()
        {
            if (GLInterop)
                GLInterop->UnmapAll();
        }
    };
#else
    struct FluidSystemGPU::CudaImpl
    {
    }; // Empty stub when CUDA disabled
#endif

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

        // ====================================================================
        // SPH 路径共享 UBO（binding=12）+ push constant 镜像
        // 7 SPH shader 用同一布局；详见 sph_density.glsl / sph_pcisph_*.glsl Vulkan 分支。
        // ====================================================================
        struct alignas(16) SPHParamsUBO
        {
            glm::vec4 GravityAndSmoothingRadius; // xyz=Gravity, w=SmoothingRadius
            glm::vec4 MassDensityGasViscosity;   // x=ParticleMass, y=RestDensity, z=GasConstant, w=Viscosity
            glm::vec4 GridParams;                // x=GridSize, y=CellSize, z=Poly6Coeff, w=SpikyCoeff
            glm::vec4 BoundaryParams; // x=BoundaryStiffness, y=BoundaryDamping, z=WarmupTime, w=SurfaceTension
            glm::vec4 SDFCounts;      // x=RigidBodyCount, y=MeshSDFCount, z=MeshSDFVoxelCount, w=PCISPHDelta
        };
        static_assert(sizeof(SPHParamsUBO) == 80, "SPHParamsUBO must be std140-aligned 80 bytes");

        struct SPHPushConstants
        {
            uint32_t AliveCount;
            float    DeltaTime;
            uint32_t UsePredictedPos;
        };
        static_assert(sizeof(SPHPushConstants) == 12, "SPHPushConstants must be 12 bytes");

        // Binding 常量（与 shader layout 一致）
        constexpr uint32_t FLUID_EMIT_UBO_BINDING = 5;
        constexpr uint32_t FLUID_SIM_UBO_BINDING  = 6;
        constexpr uint32_t FLUID_SPH_UBO_BINDING  = 12;
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
        Ref<VulkanDescriptorPool>      Pools[2];
        VkQueryPool                    TimestampPool       = VK_NULL_HANDLE;
        float                          TimestampPeriodNs   = 0.0f;
        uint32_t                       TimestampValidBits  = 0;
        bool                           TimestampWritten[2] = {false, false};

        // ---- SPH 7 pipeline ----
        bool                           SPHInitialized = false;
        Ref<VulkanDescriptorSetLayout> SPHDensityLayout;
        Ref<VulkanDescriptorSetLayout> SPHForceLayout;
        Ref<VulkanDescriptorSetLayout> PCISPHInitLayout;
        Ref<VulkanDescriptorSetLayout> PCISPHPredictLayout;
        Ref<VulkanDescriptorSetLayout> PCISPHDensityLayout;
        Ref<VulkanDescriptorSetLayout> PCISPHForceLayout;
        Ref<VulkanDescriptorSetLayout> PCISPHApplyLayout;
        VulkanComputePipelineHandle    SPHDensityPipeline{};
        VulkanComputePipelineHandle    SPHForcePipeline{};
        VulkanComputePipelineHandle    PCISPHInitPipeline{};
        VulkanComputePipelineHandle    PCISPHPredictPipeline{};
        VulkanComputePipelineHandle    PCISPHDensityPipeline{};
        VulkanComputePipelineHandle    PCISPHForcePipeline{};
        VulkanComputePipelineHandle    PCISPHApplyPipeline{};
    };
#else
    struct FluidSystemGPU::VulkanResources
    {
        // Empty stub on non-Vulkan builds (header still references the type)
    };
#endif

    FluidSystemGPU::FluidSystemGPU(uint32_t particleCount, FluidComputeBackend backend)
        : m_ParticleCount(particleCount), m_RequestedBackend(backend), m_CudaImpl(CreateScope<CudaImpl>()),
          m_VulkanResources(CreateScope<VulkanResources>())
    {
    }

    const char* FluidSystemGPU::BackendLabel(FluidComputeBackend backend)
    {
        switch (backend)
        {
        case FluidComputeBackend::Automatic:
            return "Automatic";
        case FluidComputeBackend::OpenGL:
            return "OpenGL Compute";
        case FluidComputeBackend::CUDA:
            return "CUDA";
        case FluidComputeBackend::Vulkan:
            return "Vulkan Compute";
        default:
            return "Unknown";
        }
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

        const RendererAPI::API rendererAPI     = RendererAPI::GetAPI();
        const bool             requestedVulkan = m_RequestedBackend == FluidComputeBackend::Vulkan;
        const bool             requestedGLFamily =
            m_RequestedBackend == FluidComputeBackend::OpenGL || m_RequestedBackend == FluidComputeBackend::CUDA;

        if (requestedVulkan && rendererAPI != RendererAPI::API::Vulkan)
        {
            m_BackendFailureReason = "Vulkan Compute requires the Vulkan renderer context";
            m_Initialized          = true;
            ENGINE_CORE_ERROR("[Fluid] {}", m_BackendFailureReason);
            return;
        }
        if (requestedGLFamily && rendererAPI != RendererAPI::API::OpenGL)
        {
            m_BackendFailureReason = "OpenGL Compute and CUDA require the OpenGL renderer context";
            m_Initialized          = true;
            ENGINE_CORE_ERROR("[Fluid] {}", m_BackendFailureReason);
            return;
        }

        m_ActiveBackend =
            rendererAPI == RendererAPI::API::Vulkan ? FluidComputeBackend::Vulkan : FluidComputeBackend::OpenGL;

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

            // SPH 7 个 shader 共享同一 UBO 实例（每帧首次 UpdateVulkan 内更新一次）。
            m_SPHParamsUBO = UniformBuffer::Create(sizeof(SPHParamsUBO), FLUID_SPH_UBO_BINDING);

            // SDF metadata 异步回读 ring。容量按 MeshSDFMeta 上限分配。
            m_SDFMetaReadback = GPUAsyncReadback::Create(MAX_MESH_SDF_BODIES * sizeof(GPUMeshSDFData));
        }
#endif

#ifdef ENGINE_ENABLE_CUDA
        // ---- CUDA compute sidecar 初始化（仅 OpenGL 路径 + 未强制 GL） ----
        // fluid 没有 Particle 的 forceGL AB config；CUDA 路径默认开启，可通过
        // 环境变量 ENGINE_FLUID_FORCE_GL=1 在诊断对比时停用走 GL compute。
        const bool allowCuda =
            m_RequestedBackend == FluidComputeBackend::Automatic || m_RequestedBackend == FluidComputeBackend::CUDA;
        if (allowCuda && !m_CudaImpl->InitAttempted && RendererAPI::GetAPI() == RendererAPI::API::OpenGL &&
            !CudaInterop::IsCudaPoisoned())
        {
            m_CudaImpl->InitAttempted = true;

            bool forceGL = m_RequestedBackend == FluidComputeBackend::OpenGL;
            if (m_RequestedBackend == FluidComputeBackend::Automatic)
            {
                if (const char* env = std::getenv("ENGINE_FLUID_FORCE_GL"))
                    forceGL = (env[0] == '1');
            }

            if (!forceGL && CudaGLInteropContext::ProbeDeviceMatch())
            {
                m_CudaImpl->GLInterop = CreateScope<CudaGLInteropContext>();
                m_CudaImpl->SlotParticle =
                    m_CudaImpl->GLInterop->RegisterBuffer(m_ParticleBuffer->GetRendererID(), "FluidParticleBuffer");

                if (m_CudaImpl->SlotParticle >= 0)
                {
                    m_CudaImpl->SPHCtx = CudaInterop::CreateSPHContext(m_ParticleCount, 64, MAX_RIGID_BODIES);
                    if (m_CudaImpl->SPHCtx)
                    {
                        m_CudaImpl->UseCudaPath = true;
                        m_CudaImpl->Timing.Init();
                        m_ActiveBackend = FluidComputeBackend::CUDA;
                        ENGINE_CORE_INFO("[Fluid][CUDA] FluidSystemGPU CUDA 路径启用 ({0} slots)",
                                         m_CudaImpl->GLInterop->GetSlotCount());
                    }
                    else
                    {
                        m_BackendFailureReason = "Failed to create CUDA SPH context";
                        ENGINE_WARN("[Fluid][CUDA] {}", m_BackendFailureReason);
                        m_CudaImpl->GLInterop.reset();
                    }
                }
                else
                {
                    m_BackendFailureReason = "Failed to register the fluid particle buffer with CUDA";
                    ENGINE_WARN("[Fluid][CUDA] RegisterBuffer failed; CUDA path disabled");
                    m_CudaImpl->GLInterop.reset();
                }
            }
            else if (!forceGL)
            {
                m_BackendFailureReason = "CUDA device does not match the active OpenGL device";
            }
        }
#else
        if (m_RequestedBackend == FluidComputeBackend::CUDA)
            m_BackendFailureReason = "CUDA support is not compiled into this build";
#endif

        if (m_RequestedBackend == FluidComputeBackend::CUDA && m_ActiveBackend != FluidComputeBackend::CUDA)
        {
            if (m_BackendFailureReason.empty())
            {
#ifdef ENGINE_ENABLE_CUDA
                m_BackendFailureReason = CudaInterop::IsCudaPoisoned() ? CudaInterop::GetCudaPoisonReason()
                                                                       : "CUDA backend initialization failed";
#else
                m_BackendFailureReason = "CUDA support is not compiled into this build";
#endif
            }
            m_BackendReady = false;
            m_Initialized  = true;
            ENGINE_CORE_ERROR("[Fluid][CUDA] Strict backend request failed: {}", m_BackendFailureReason);
            return;
        }

#ifdef ENGINE_ENABLE_VULKAN
        if (m_ActiveBackend == FluidComputeBackend::Vulkan && !InitVulkanComputeResources())
        {
            m_BackendReady = false;
            m_Initialized  = true;
            ENGINE_CORE_ERROR("[Fluid][Vulkan] Strict backend request failed: {}", m_BackendFailureReason);
            return;
        }
#endif

        m_BackendReady = true;
        m_Initialized  = true;
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

    bool FluidSystemGPU::SetBenchmarkParticles(const std::vector<FluidBenchmarkParticle>& particles)
    {
        if (!m_Initialized || !m_BackendReady || !m_ParticleBuffer || particles.size() != m_ParticleCount)
            return false;

        RenderCommand::WaitIdle();
        m_ParticleBuffer->SetData(particles.data(),
                                  static_cast<uint32_t>(particles.size() * sizeof(FluidBenchmarkParticle)));
        m_TotalTime        = 0.0f;
        m_LastTimingSample = {};
        return true;
    }

    bool FluidSystemGPU::ReadBenchmarkParticles(std::vector<FluidBenchmarkParticle>& particles,
                                                std::string&                         error) const
    {
        error.clear();
        if (!m_Initialized || !m_BackendReady || !m_ParticleBuffer)
        {
            error = m_BackendFailureReason.empty() ? "fluid backend is not ready" : m_BackendFailureReason;
            return false;
        }

        RenderCommand::WaitIdle();
        particles.resize(m_ParticleCount);
        m_ParticleBuffer->GetData(particles.data(),
                                  static_cast<uint32_t>(particles.size() * sizeof(FluidBenchmarkParticle)));
        return true;
    }

    void FluidSystemGPU::PublishTimingSample(FluidComputeBackend backend, float computeMs, float interopMs)
    {
        m_LastTimingSample.Backend   = backend;
        m_LastTimingSample.ComputeMs = computeMs;
        m_LastTimingSample.InteropMs = interopMs;
        ++m_LastTimingSample.Sequence;
        m_LastTimingSample.Valid = true;
    }

    void FluidSystemGPU::SetBenchmarkTimingReadback(bool blocking)
    {
#ifdef ENGINE_ENABLE_CUDA
        m_CudaImpl->Timing.SetBlockingReadback(blocking);
#else
        (void)blocking;
#endif
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

#ifdef ENGINE_ENABLE_CUDA
        if (m_CudaImpl->UseCudaPath && !CudaInterop::IsCudaPoisoned())
        {
            bool cudaRan = UpdateCuda(clampedDt, emitterPos, emitter, registry, kp);
            PerformanceMonitor::Get().SetFluidComputeCudaActive(cudaRan);
            if (cudaRan)
                return;
            if (m_RequestedBackend == FluidComputeBackend::CUDA)
            {
                m_BackendReady = false;
                m_BackendFailureReason =
                    CudaInterop::IsCudaPoisoned() ? CudaInterop::GetCudaPoisonReason() : "CUDA dispatch failed";
                ENGINE_CORE_ERROR("[Fluid][CUDA] Strict backend request failed during update: {}",
                                  m_BackendFailureReason);
                return;
            }
            m_ActiveBackend = FluidComputeBackend::OpenGL;
            // Automatic 模式下 CUDA 失败 → fall through 到 GL compute 路径
        }
        PerformanceMonitor::Get().SetFluidComputeCudaActive(false);
#endif

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

            // 所有迭代复用初始 grid（性能优化：跳过迭代 1+ 的网格重建开销）
            for (int iter = 0; iter < iterations; iter++)
            {
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
                m_SPHShaders.PCISPHDensity->SetInt("u_UsePredictedPos", 0);
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
                m_SPHShaders.PCISPHForce->SetInt("u_UsePredictedPos", 0);
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

        auto& fluidTimer = PerformanceMonitor::Get().GetFluidComputeGPUTimer();
        fluidTimer.End();
        if (fluidTimer.HasValidResult())
            PublishTimingSample(FluidComputeBackend::OpenGL, fluidTimer.GetElapsedMs());

        PerformanceMonitor::Get().SetFluidActive(true);
    }

#ifdef ENGINE_ENABLE_CUDA
    // =========================================================================
    // CUDA 路径实现 —— SPH (Grid Build / Density / Force 或 PCISPH 全套) +
    // FluidSimulate（积分 + 边界）。Emit 走 GL，Update 走 CUDA。失败时 fallback 到 GL。
    //
    // fluid 没有 Particle 的 dead list / counter buffer——所有 m_ParticleCount 个粒子
    // 都是 alive 的，SPH 内核以 aliveCount=m_ParticleCount 直接 iterate particles[0..N-1]，
    // 而 SPHCtx 内部维护 sortedIndices（CUB prefix sum 后的紧凑顺序）。
    // =========================================================================
    bool FluidSystemGPU::UpdateCuda(float                        clampedDt,
                                    const glm::vec3&             emitterPos,
                                    const FluidEmitterComponent& emitter,
                                    entt::registry*              registry,
                                    const SPHKernelParams&       kp)
    {
        if (!m_CudaImpl->GLInterop || !m_CudaImpl->SPHCtx)
            return false;

        const auto mapStart = std::chrono::steady_clock::now();
        if (!m_CudaImpl->MapAll())
        {
            ENGINE_WARN("[Fluid][CUDA] MapAll failed; falling back to GL compute.");
            return false;
        }
        const auto mapEnd    = std::chrono::steady_clock::now();
        float      interopMs = std::chrono::duration<float, std::milli>(mapEnd - mapStart).count();

        cudaStream_t strm         = static_cast<cudaStream_t>(m_CudaImpl->GetStream());
        void*        devParticles = m_CudaImpl->GetMappedPointer(m_CudaImpl->SlotParticle);
        if (!devParticles)
        {
            m_CudaImpl->UnmapAll();
            return false;
        }

        m_CudaImpl->Timing.RecordStart(strm);

        const float cellSize = 2.0f * emitter.SmoothingRadius;
        const int   gridSize = static_cast<int>(m_Grid.GetGridSize());

        CudaInterop::SPHParams sphP{};
        sphP.smoothingRadius = emitter.SmoothingRadius;
        sphP.poly6Coeff      = kp.poly6Coeff;
        sphP.spikyCoeff      = kp.spikyCoeff;
        sphP.particleMass    = emitter.ParticleMass;
        sphP.restDensity     = emitter.RestDensity;
        sphP.gasConstant     = emitter.GasConstant;
        sphP.viscosity       = emitter.Viscosity;
        sphP.surfaceTension  = emitter.SurfaceTension;
        sphP.deltaTime       = clampedDt;
        sphP.gridSize        = gridSize;
        sphP.cellSize        = cellSize;
        sphP.aliveCount      = static_cast<int>(m_ParticleCount);
        sphP.gravity[0]      = emitter.Gravity.x;
        sphP.gravity[1]      = emitter.Gravity.y;
        sphP.gravity[2]      = emitter.Gravity.z;
        sphP.warmupTime      = 0.0f; // fluid 永久存活，无 warm-up concept

        // 收集刚体数据并上传到 SPHCtx
        std::vector<GPURigidBodyData> rigidBodies;
        if (emitter.RigidBodyCoupling && registry)
        {
            rigidBodies = CollectRigidBodies(registry, MAX_RIGID_BODIES, RigidBodyUploadFilter::AllColliders);
            if (!rigidBodies.empty())
                CudaInterop::SPHUploadRigidBodies(m_CudaImpl->SPHCtx, rigidBodies.data(),
                                                  static_cast<uint32_t>(rigidBodies.size()));
        }
        const uint32_t rigidBodyCount = static_cast<uint32_t>(rigidBodies.size());

        // 卧槽：fluid CUDA 路径暂不实现 Mesh SDF 体素耦合——Mesh SDF 在 CUDA 端需
        // 体素 d_volume 复制进 SPHCtx，当前 CudaSPHPipeline 内核不接受 Mesh SDF。
        // 留 TODO，需要时扩展 SPHContextImpl + ForceKernel。当前 meshSDFCoupling
        // 在 CUDA 路径下静默失效。

        // Pass a: Grid Build
        CudaInterop::LaunchSPHGridBuild(m_CudaImpl->SPHCtx, devParticles, m_ParticleCount, gridSize, cellSize, strm);

        // 与 OpenGL/Vulkan 保持相同顺序：PCISPH Init 也依赖当前粒子密度，
        // 因此基础 density pass 不能只在 WCSPH 分支执行。
        CudaInterop::LaunchSPHDensity(m_CudaImpl->SPHCtx, devParticles, sphP, strm);

        if (emitter.PCISPHEnabled)
        {
            // Pass b: PCISPH 全套
            CudaInterop::PCISPHIterParams ip{};
            ip.pcisphDelta       = SPHKernelMath::ComputePCISPHDelta(emitter.SmoothingRadius, emitter.ParticleMass,
                                                                     emitter.RestDensity, clampedDt);
            ip.boundaryStiffness = emitter.BoundaryStiffness;
            ip.boundaryDamping   = emitter.BoundaryDamping;
            ip.rigidBodyCount    = static_cast<int>(rigidBodyCount);
            ip.usePredictedPos   = 0;

            CudaInterop::LaunchPCISPHInit(m_CudaImpl->SPHCtx, devParticles, sphP, strm);

            int iterations = std::clamp(emitter.PCISPHIterations, 1, 8);
            for (int iter = 0; iter < iterations; ++iter)
            {
                // 与 OpenGL 基准路径一致：所有迭代复用原始位置构建的网格。
                ip.usePredictedPos = 0;
                CudaInterop::LaunchPCISPHPredict(m_CudaImpl->SPHCtx, devParticles, clampedDt,
                                                 static_cast<int>(m_ParticleCount), strm);
                CudaInterop::LaunchPCISPHDensity(m_CudaImpl->SPHCtx, devParticles, sphP, ip, strm);
                CudaInterop::LaunchPCISPHForce(m_CudaImpl->SPHCtx, devParticles, sphP, ip, strm);
            }

            CudaInterop::LaunchPCISPHApply(m_CudaImpl->SPHCtx, devParticles, static_cast<int>(m_ParticleCount), true,
                                           strm);
        }
        else
        {
            // Pass b alt: WCSPH Density + Force
            CudaInterop::PCISPHIterParams ip{};
            ip.boundaryStiffness = emitter.BoundaryStiffness;
            ip.boundaryDamping   = emitter.BoundaryDamping;
            ip.rigidBodyCount    = static_cast<int>(rigidBodyCount);
            ip.usePredictedPos   = 0;

            CudaInterop::LaunchSPHForce(m_CudaImpl->SPHCtx, devParticles, sphP, ip, strm);
        }

        // Pass c: Fluid simulate（积分 + 边界约束）
        CudaInterop::SPHSimulateParams sp{};
        sp.deltaTime      = clampedDt;
        sp.damping        = emitter.Damping;
        sp.gravity[0]     = emitter.PCISPHEnabled ? 0.0f : emitter.Gravity.x;
        sp.gravity[1]     = emitter.PCISPHEnabled ? 0.0f : emitter.Gravity.y;
        sp.gravity[2]     = emitter.PCISPHEnabled ? 0.0f : emitter.Gravity.z;
        sp.boundaryMin[0] = emitterPos.x + emitter.BoundaryMin.x;
        sp.boundaryMin[1] = emitterPos.y + emitter.BoundaryMin.y;
        sp.boundaryMin[2] = emitterPos.z + emitter.BoundaryMin.z;
        sp.boundaryMax[0] = emitterPos.x + emitter.BoundaryMax.x;
        sp.boundaryMax[1] = emitterPos.y + emitter.BoundaryMax.y;
        sp.boundaryMax[2] = emitterPos.z + emitter.BoundaryMax.z;
        sp.useBoundary    = emitter.UseBoundary ? 1 : 0;
        sp.particleCount  = static_cast<int>(m_ParticleCount);
        sp.pcisphMode     = emitter.PCISPHEnabled ? 1 : 0;
        CudaInterop::LaunchSPHSimulate(devParticles, sp, strm);

        m_CudaImpl->Timing.RecordStop(strm);
        const auto unmapStart = std::chrono::steady_clock::now();
        m_CudaImpl->UnmapAll();
        const auto unmapEnd = std::chrono::steady_clock::now();
        interopMs += std::chrono::duration<float, std::milli>(unmapEnd - unmapStart).count();

        if (CudaInterop::IsCudaPoisoned())
        {
            ENGINE_WARN("[Fluid][CUDA] Compute poisoned during dispatch; falling back to GL compute.");
            return false;
        }

        // 每帧 Swap CUDA ping-pong events，上一帧已完成的耗时喂给 PerformanceMonitor。
        // cudaRan==true（双 Pass 都成功 RecordStop 后）才 Swap，避免孤立 start 事件污染下一帧。
        m_CudaImpl->Timing.SwapEvents();
        float ms = m_CudaImpl->Timing.GetPrevElapsedMs();
        if (ms >= 0.0f)
        {
            PerformanceMonitor::Get().SetFluidComputeCudaMs(ms);
            PublishTimingSample(FluidComputeBackend::CUDA, ms, interopMs);
        }

        PerformanceMonitor::Get().SetFluidActive(true);
        return true;
    }
#endif

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

        // 容量扩到 256：emit+simulate ≤ 2/帧；SPH 路径 PCISPH 8 迭代 × 3 dispatch + density/init/apply + warmup
        // 余量 ≈ 32 set/帧（保险 ×8 倍）。pool 创建按 sets/type 论参数，最终 maxSets = N×8。
        for (auto& pool : m_VulkanResources->Pools)
            pool = VulkanDescriptorPool::CreateDefaultComputePool(m_VulkanResources->Device, 256);

        auto failTimestampSetup = [&](const std::string& reason)
        {
            m_BackendFailureReason         = reason;
            m_VulkanResources->Initialized = true;
            ENGINE_CORE_ERROR("[Fluid][Vulkan] {}", m_BackendFailureReason);
            DestroyVulkanComputeResources();
            return false;
        };

        VkPhysicalDeviceProperties deviceProperties{};
        vkGetPhysicalDeviceProperties(ctx->GetPhysicalDevice(), &deviceProperties);

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(ctx->GetPhysicalDevice(), &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(ctx->GetPhysicalDevice(), &queueFamilyCount, queueFamilies.data());

        const uint32_t graphicsQueueFamily = ctx->GetGraphicsQueueFamily();
        if (!deviceProperties.limits.timestampComputeAndGraphics || graphicsQueueFamily >= queueFamilies.size() ||
            queueFamilies[graphicsQueueFamily].timestampValidBits == 0)
        {
            return failTimestampSetup("Vulkan compute timestamps are not supported by the active graphics queue");
        }

        VkQueryPoolCreateInfo queryInfo{};
        queryInfo.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryInfo.queryType  = VK_QUERY_TYPE_TIMESTAMP;
        queryInfo.queryCount = 4;
        if (vkCreateQueryPool(m_VulkanResources->Device, &queryInfo, nullptr, &m_VulkanResources->TimestampPool) !=
            VK_SUCCESS)
        {
            return failTimestampSetup("Failed to create Vulkan timestamp query pool");
        }
        m_VulkanResources->TimestampPeriodNs  = deviceProperties.limits.timestampPeriod;
        m_VulkanResources->TimestampValidBits = queueFamilies[graphicsQueueFamily].timestampValidBits;

        m_VulkanResources->Initialized = true;
        ENGINE_CORE_INFO("[Fluid][Vulkan] 流体 compute pipeline 初始化完成 (emit + simulate, timestampPeriod={}ns)",
                         m_VulkanResources->TimestampPeriodNs);
        return true;
    }

    // SPH 7 pipeline 懒初始化（仅 Vulkan 路径 SPH 启用时调用）
    bool FluidSystemGPU::InitSPHVulkanPipelines()
    {
        if (m_VulkanResources->SPHInitialized)
            return true;

        VkDevice device = m_VulkanResources->Device;

        auto buildOne = [&](const Ref<Shader>& shader, Ref<VulkanDescriptorSetLayout>& outLayout,
                            VulkanComputePipelineHandle& outPipe) -> bool
        {
            auto vkShader = std::dynamic_pointer_cast<VulkanShader>(shader);
            ENGINE_CORE_RELEASE_ASSERT(vkShader, "[Fluid][Vulkan] SPH shader 转型失败");

            VkShaderModule module = vkShader->GetOrCreateShaderModule(device, "compute");
            ENGINE_CORE_RELEASE_ASSERT(module != VK_NULL_HANDLE, "[Fluid][Vulkan] SPH shader module 创建失败");

            outLayout = VulkanDescriptorSetLayout::CreateFromReflection(device, vkShader->GetReflectedBindings(), 0);

            VulkanComputePipelineDesc desc{};
            desc.ShaderModule = module;
            desc.EntryPoint   = "main";
            desc.SetLayouts   = {outLayout->GetHandle()};
            for (const auto& pc : vkShader->GetReflectedPushConstants())
            {
                VkPushConstantRange r{};
                r.offset     = pc.Offset;
                r.size       = pc.Size;
                r.stageFlags = pc.Stages;
                desc.PushConstants.push_back(r);
            }
            outPipe = VulkanPipeline::CreateCompute(device, desc);
            return true;
        };

        buildOne(m_SPHShaders.DensityShader, m_VulkanResources->SPHDensityLayout,
                 m_VulkanResources->SPHDensityPipeline);
        buildOne(m_SPHShaders.ForceShader, m_VulkanResources->SPHForceLayout, m_VulkanResources->SPHForcePipeline);
        buildOne(m_SPHShaders.PCISPHInit, m_VulkanResources->PCISPHInitLayout, m_VulkanResources->PCISPHInitPipeline);
        buildOne(m_SPHShaders.PCISPHPredict, m_VulkanResources->PCISPHPredictLayout,
                 m_VulkanResources->PCISPHPredictPipeline);
        buildOne(m_SPHShaders.PCISPHDensity, m_VulkanResources->PCISPHDensityLayout,
                 m_VulkanResources->PCISPHDensityPipeline);
        buildOne(m_SPHShaders.PCISPHForce, m_VulkanResources->PCISPHForceLayout,
                 m_VulkanResources->PCISPHForcePipeline);
        buildOne(m_SPHShaders.PCISPHApply, m_VulkanResources->PCISPHApplyLayout,
                 m_VulkanResources->PCISPHApplyPipeline);

        m_VulkanResources->SPHInitialized = true;
        ENGINE_CORE_INFO("[Fluid][Vulkan] SPH 7 compute pipeline 初始化完成 "
                         "(density/force + pcisph_{init,predict,density,force,apply})");
        return true;
    }

    void FluidSystemGPU::DestroyVulkanComputeResources()
    {
        if (!m_VulkanResources || !m_VulkanResources->Initialized)
            return;

        if (m_VulkanResources->Device != VK_NULL_HANDLE)
            vkDeviceWaitIdle(m_VulkanResources->Device);

        if (m_VulkanResources->TimestampPool != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(m_VulkanResources->Device, m_VulkanResources->TimestampPool, nullptr);
            m_VulkanResources->TimestampPool = VK_NULL_HANDLE;
        }

        VulkanPipeline::DestroyCompute(m_VulkanResources->Device, m_VulkanResources->EmitPipeline);
        VulkanPipeline::DestroyCompute(m_VulkanResources->Device, m_VulkanResources->SimulatePipeline);

        if (m_VulkanResources->SPHInitialized)
        {
            VulkanPipeline::DestroyCompute(m_VulkanResources->Device, m_VulkanResources->SPHDensityPipeline);
            VulkanPipeline::DestroyCompute(m_VulkanResources->Device, m_VulkanResources->SPHForcePipeline);
            VulkanPipeline::DestroyCompute(m_VulkanResources->Device, m_VulkanResources->PCISPHInitPipeline);
            VulkanPipeline::DestroyCompute(m_VulkanResources->Device, m_VulkanResources->PCISPHPredictPipeline);
            VulkanPipeline::DestroyCompute(m_VulkanResources->Device, m_VulkanResources->PCISPHDensityPipeline);
            VulkanPipeline::DestroyCompute(m_VulkanResources->Device, m_VulkanResources->PCISPHForcePipeline);
            VulkanPipeline::DestroyCompute(m_VulkanResources->Device, m_VulkanResources->PCISPHApplyPipeline);

            m_VulkanResources->SPHDensityLayout.reset();
            m_VulkanResources->SPHForceLayout.reset();
            m_VulkanResources->PCISPHInitLayout.reset();
            m_VulkanResources->PCISPHPredictLayout.reset();
            m_VulkanResources->PCISPHDensityLayout.reset();
            m_VulkanResources->PCISPHForceLayout.reset();
            m_VulkanResources->PCISPHApplyLayout.reset();
            m_VulkanResources->SPHInitialized = false;
        }

        for (auto& pool : m_VulkanResources->Pools)
            pool.reset();
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

        // Lazy init SPH grid
        if (!m_SPHInitialized)
            InitSPH(emitter.SmoothingRadius);

        VkDevice            device = m_VulkanResources->Device;
        VulkanCommandBuffer cmdBuf(cmd);

        const uint32_t frameIndex = ctx->GetCurrentFrameIndex();
        const uint32_t queryBase  = frameIndex * 2;
        if (m_VulkanResources->TimestampWritten[frameIndex])
        {
            uint64_t       timestamps[2] = {};
            const VkResult queryResult =
                vkGetQueryPoolResults(device, m_VulkanResources->TimestampPool, queryBase, 2, sizeof(timestamps),
                                      timestamps, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
            if (queryResult == VK_SUCCESS)
            {
                uint64_t elapsedTicks = timestamps[1] - timestamps[0];
                if (m_VulkanResources->TimestampValidBits < 64)
                {
                    const uint64_t validMask = (uint64_t{1} << m_VulkanResources->TimestampValidBits) - 1;
                    elapsedTicks &= validMask;
                }
                const float elapsedMs = static_cast<float>(static_cast<double>(elapsedTicks) *
                                                           m_VulkanResources->TimestampPeriodNs / 1000000.0);
                PublishTimingSample(FluidComputeBackend::Vulkan, elapsedMs);
            }
            else if (queryResult != VK_NOT_READY)
            {
                m_BackendReady         = false;
                m_BackendFailureReason = "Failed to read Vulkan timestamp query results";
                ENGINE_CORE_ERROR("[Fluid][Vulkan] {} ({})", m_BackendFailureReason, static_cast<int>(queryResult));
                return;
            }
        }

        vkCmdResetQueryPool(cmd, m_VulkanResources->TimestampPool, queryBase, 2);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_VulkanResources->TimestampPool, queryBase);

        // 每个 in-flight frame 独占一个 descriptor pool；BeginFrame 已等待该 frame slot 的 fence，
        // 因此此处 Reset 不会使另一帧仍在使用的 descriptor set 失效。
        Ref<VulkanDescriptorPool>& descriptorPool = m_VulkanResources->Pools[frameIndex];
        descriptorPool->Reset();

        // 取四元组 ShaderStorage barrier（每 dispatch 后插一次）
        const VulkanBarrierMasks ssboBarrier   = ResolveBarrierBits(BarrierBit::ShaderStorage);
        auto                     ssboBarrierFn = [&]()
        {
            cmdBuf.MemoryBarrier(ssboBarrier.SrcStage, ssboBarrier.DstStage, ssboBarrier.SrcAccess,
                                 ssboBarrier.DstAccess);
        };

        // 缓存 SSBO → VkBuffer 工具
        auto bufferOf = [](const Ref<ShaderStorageBuffer>& ssbo) -> VkBuffer
        {
            auto v = std::dynamic_pointer_cast<VulkanStorageBuffer>(ssbo);
            ENGINE_CORE_RELEASE_ASSERT(v, "[Fluid][Vulkan] SSBO 转型失败");
            return v->GetBuffer();
        };

        auto vkSimUBO = std::dynamic_pointer_cast<VulkanUniformBuffer>(m_SimParamsUBO);
        auto vkSPHUBO = std::dynamic_pointer_cast<VulkanUniformBuffer>(m_SPHParamsUBO);
        ENGINE_CORE_RELEASE_ASSERT(vkSimUBO, "[Fluid][Vulkan] sim UBO 转型失败");

        // ============================================================
        // SPH 主流程（D-3：录入主帧 cmd）
        // 与 OpenGL 路径对齐：先 density → (PCISPH 8 迭代 | WCSPH force) → simulate
        // ============================================================
        // SPH 流体永远启用 SPH（区别于 ParticleSystemGPU 受 emitter.SPH.Enabled 控制）
        bool                  sphEnabled     = m_SPHInitialized;
        const SPHKernelParams kp             = SPHKernelParams::Compute(emitter.SmoothingRadius);
        uint32_t              rigidBodyCount = 0;
        uint32_t              meshSDFCount   = 0;
        uint32_t              meshSDFVoxels  = 0;
        float                 meshSDFBuildMs = 0.0f;
        float                 cellSize       = m_Grid.GetCellSize();
        uint32_t              gridSize       = m_Grid.GetGridSize();
        float pcisphDelta = SPHKernelMath::ComputePCISPHDelta(emitter.SmoothingRadius, emitter.ParticleMass,
                                                              emitter.RestDensity, clampedDt);

        // 懒初始化 SPH pipeline；失败则跳过 SPH 段（不影响 simulate dispatch）
        if (sphEnabled && !InitSPHVulkanPipelines())
        {
            ENGINE_CORE_WARN("[Fluid][Vulkan] SPH pipeline 初始化失败，跳过 SPH dispatch");
            sphEnabled = false;
        }

        if (sphEnabled)
        {
            ENGINE_CORE_RELEASE_ASSERT(vkSPHUBO, "[Fluid][Vulkan] SPH UBO 未创建");

            // ---- 上传 rigid/mesh SDF（与 OpenGL UpdateSPH 段对齐）----
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

            // 调试可视化数据（PCISPH/WCSPH 都收集）
            m_MeshSDFDebugBodies.clear();
            m_MeshSDFDebugStats.Enabled          = emitter.MeshSDFCoupling;
            m_MeshSDFDebugStats.BodyCount        = meshSDFCount;
            m_MeshSDFDebugStats.VoxelCount       = meshSDFVoxels;
            m_MeshSDFDebugStats.EstimatedSamples = meshSDFCount * m_ParticleCount;
            m_MeshSDFDebugStats.Resolution       = static_cast<uint32_t>(std::max(emitter.MeshSDFResolution, 0));
            m_MeshSDFDebugStats.Band             = emitter.MeshSDFBand;
            m_MeshSDFDebugStats.LastBuildCpuMs   = meshSDFBuildMs;

            // ---- 上传 SPHParams UBO（stable params + PCISPH delta）----
            SPHParamsUBO sphUbo{};
            sphUbo.GravityAndSmoothingRadius = glm::vec4(emitter.Gravity, emitter.SmoothingRadius);
            sphUbo.MassDensityGasViscosity =
                glm::vec4(emitter.ParticleMass, emitter.RestDensity, emitter.GasConstant, emitter.Viscosity);
            sphUbo.GridParams     = glm::vec4(static_cast<float>(gridSize), cellSize, kp.poly6Coeff, kp.spikyCoeff);
            sphUbo.BoundaryParams = glm::vec4(emitter.BoundaryStiffness, emitter.BoundaryDamping,
                                              /*WarmupTime=*/0.0f, emitter.SurfaceTension);
            sphUbo.SDFCounts      = glm::vec4(static_cast<float>(rigidBodyCount), static_cast<float>(meshSDFCount),
                                              static_cast<float>(meshSDFVoxels), pcisphDelta);
            m_SPHParamsUBO->SetData(&sphUbo, sizeof(sphUbo));

            // ---- Grid 注入外部 buffer + 每帧首次 ResetFrameResources ----
            // PCISPH 模式下 m_PCISPHBuffer 在 InitPCISPH 后可用；若 emitter 未开 PCISPH 则保持 nullptr
            if (emitter.PCISPHEnabled)
                InitPCISPH();

            m_Grid.SetExternalBuffers(m_ParticleBuffer, m_AliveList, emitter.PCISPHEnabled ? m_PCISPHBuffer : nullptr);
            m_Grid.ResetFrameResources();
            m_Grid.BuildVulkan(cmd, m_ParticleCount, /*predicted=*/false);
            ssboBarrierFn();

            // 各 SPH dispatch 共用：alloc set → write SSBO+UBO → bind → PC → dispatch → barrier
            auto dispatchSPH = [&](const VulkanComputePipelineHandle&    pipe,
                                   const Ref<VulkanDescriptorSetLayout>& layout, bool bindPCISPH, bool bindRigid,
                                   bool bindMeshSDF, bool bindSurfaceNormal, bool bindGrid, uint32_t usePredictedPos)
            {
                VkDescriptorSet set = descriptorPool->Allocate(layout->GetHandle());
                ENGINE_CORE_RELEASE_ASSERT(set != VK_NULL_HANDLE, "[Fluid][Vulkan] SPH pool 耗尽");

                VulkanDescriptorWriter w;
                w.WriteBuffer(0, bufferOf(m_ParticleBuffer), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                if (bindPCISPH && m_PCISPHBuffer)
                    w.WriteBuffer(1, bufferOf(m_PCISPHBuffer), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                w.WriteBuffer(2, bufferOf(m_AliveList), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                if (bindRigid && m_RigidBodyBuffer)
                    w.WriteBuffer(3, bufferOf(m_RigidBodyBuffer), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                if (bindGrid)
                {
                    w.WriteBuffer(5, bufferOf(m_Grid.GetCellStart()), 0, VK_WHOLE_SIZE,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                    w.WriteBuffer(6, bufferOf(m_Grid.GetCellCount()), 0, VK_WHOLE_SIZE,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                    w.WriteBuffer(7, bufferOf(m_Grid.GetSortedIndices()), 0, VK_WHOLE_SIZE,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                }
                if (bindSurfaceNormal && m_SurfaceNormalBuffer)
                    w.WriteBuffer(8, bufferOf(m_SurfaceNormalBuffer), 0, VK_WHOLE_SIZE,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                if (bindMeshSDF && m_MeshSDFMetaBuffer)
                    w.WriteBuffer(10, bufferOf(m_MeshSDFMetaBuffer), 0, VK_WHOLE_SIZE,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                if (bindMeshSDF && m_MeshSDFVoxelBuffer)
                    w.WriteBuffer(11, bufferOf(m_MeshSDFVoxelBuffer), 0, VK_WHOLE_SIZE,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                w.WriteBuffer(FLUID_SPH_UBO_BINDING, vkSPHUBO->GetBuffer(), 0, sizeof(SPHParamsUBO),
                              VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
                w.UpdateSet(device, set);

                cmdBuf.BindComputePipeline(pipe.Pipeline);
                cmdBuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, pipe.Layout, 0, {set});

                SPHPushConstants pcVal{};
                pcVal.AliveCount      = m_ParticleCount;
                pcVal.DeltaTime       = clampedDt;
                pcVal.UsePredictedPos = usePredictedPos;
                cmdBuf.PushConstants(pipe.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcVal), &pcVal);

                uint32_t groups = (m_ParticleCount + 255) / 256;
                if (groups > 0)
                    cmdBuf.Dispatch(groups, 1, 1);
                ssboBarrierFn();
            };

            // ---- SPH Density ----
            // bindings: 0(P), 2(alive), 5/6/7(grid), 8(SN write)
            dispatchSPH(m_VulkanResources->SPHDensityPipeline, m_VulkanResources->SPHDensityLayout,
                        /*PCISPH=*/false, /*Rigid=*/false, /*MeshSDF=*/false, /*SN=*/true,
                        /*Grid=*/true, /*UsePred=*/0u);

            if (emitter.PCISPHEnabled)
            {
                // ---- PCISPH Init ----
                // bindings: 0(P), 1(PCISPH), 2(alive), 5/6/7(grid), 8(SN read)
                dispatchSPH(m_VulkanResources->PCISPHInitPipeline, m_VulkanResources->PCISPHInitLayout,
                            /*PCISPH=*/true, /*Rigid=*/false, /*MeshSDF=*/false, /*SN=*/true,
                            /*Grid=*/true, /*UsePred=*/0u);

                int iterations = std::clamp(emitter.PCISPHIterations, 1, 8);
                for (int iter = 0; iter < iterations; ++iter)
                {
                    // 与 OpenGL/CUDA 基准路径一致：所有迭代复用原始位置构建的网格。
                    constexpr uint32_t usePred = 0u;

                    // Predict: bindings 0(P), 1(PCISPH), 2(alive)
                    dispatchSPH(m_VulkanResources->PCISPHPredictPipeline, m_VulkanResources->PCISPHPredictLayout,
                                /*PCISPH=*/true, /*Rigid=*/false, /*MeshSDF=*/false, /*SN=*/false,
                                /*Grid=*/false, usePred);
                    // Density: bindings 0(P), 1(PCISPH), 2(alive), 5/6/7(grid)
                    dispatchSPH(m_VulkanResources->PCISPHDensityPipeline, m_VulkanResources->PCISPHDensityLayout,
                                /*PCISPH=*/true, /*Rigid=*/false, /*MeshSDF=*/false, /*SN=*/false,
                                /*Grid=*/true, usePred);
                    // Force: bindings 0(P), 1(PCISPH), 2(alive), 3(rigid), 5/6/7(grid), 10/11(meshSDF)
                    dispatchSPH(m_VulkanResources->PCISPHForcePipeline, m_VulkanResources->PCISPHForceLayout,
                                /*PCISPH=*/true, /*Rigid=*/true, /*MeshSDF=*/true, /*SN=*/false,
                                /*Grid=*/true, usePred);
                }

                // Apply: bindings 0(P), 1(PCISPH), 2(alive)
                dispatchSPH(m_VulkanResources->PCISPHApplyPipeline, m_VulkanResources->PCISPHApplyLayout,
                            /*PCISPH=*/true, /*Rigid=*/false, /*MeshSDF=*/false, /*SN=*/false,
                            /*Grid=*/false, /*UsePred=*/0u);
            }
            else
            {
                // ---- WCSPH Force ----
                // bindings: 0(P), 2(alive), 3(rigid), 5/6/7(grid), 8(SN read), 10/11(meshSDF)
                dispatchSPH(m_VulkanResources->SPHForcePipeline, m_VulkanResources->SPHForceLayout,
                            /*PCISPH=*/false, /*Rigid=*/true, /*MeshSDF=*/true, /*SN=*/true,
                            /*Grid=*/true, /*UsePred=*/0u);
            }
        }

        // ============================================================
        // fluid_simulate dispatch（积分位置/速度 + 边界）
        // ============================================================
        {
            FluidSimParamsUBO simUbo{};
            glm::vec3         simGravity = emitter.PCISPHEnabled ? glm::vec3(0.0f) : emitter.Gravity;
            simUbo.GravityAndDamping     = glm::vec4(simGravity, emitter.Damping);
            simUbo.BoundaryMinAndUseFlag =
                glm::vec4(emitterPos + emitter.BoundaryMin, emitter.UseBoundary ? 1.0f : 0.0f);
            simUbo.BoundaryMaxAndMode =
                glm::vec4(emitterPos + emitter.BoundaryMax, emitter.PCISPHEnabled ? 1.0f : 0.0f);
            m_SimParamsUBO->SetData(&simUbo, sizeof(FluidSimParamsUBO));

            VkDescriptorSet simulateSet = descriptorPool->Allocate(m_VulkanResources->SimulateLayout->GetHandle());
            ENGINE_CORE_RELEASE_ASSERT(simulateSet != VK_NULL_HANDLE,
                                       "[Fluid][Vulkan] simulate DescriptorPool 分配失败");

            auto vkParticle = std::dynamic_pointer_cast<VulkanStorageBuffer>(m_ParticleBuffer);
            ENGINE_CORE_RELEASE_ASSERT(vkParticle, "[Fluid][Vulkan] particle 转型失败");

            VulkanDescriptorWriter w;
            w.WriteBuffer(0, vkParticle->GetBuffer(), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(FLUID_SIM_UBO_BINDING, vkSimUBO->GetBuffer(), 0, sizeof(FluidSimParamsUBO),
                          VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
            w.UpdateSet(device, simulateSet);

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

            ssboBarrierFn();
        }

        // ============================================================
        // MeshSDFMeta 异步回读（PCISPH/WCSPH SDF 调试用，SPH 段接通后自然生效）
        // ============================================================
        if (m_SDFMetaReadback && m_MeshSDFMetaBuffer)
        {
            if (m_SDFMetaReadback->IsPending() && m_SDFMetaReadback->IsReady())
            {
                std::vector<GPUMeshSDFData> staging(MAX_MESH_SDF_BODIES);
                m_SDFMetaReadback->GetData(staging.data(), MAX_MESH_SDF_BODIES * sizeof(GPUMeshSDFData));

                // 用上一帧（实际为 3 帧前）异步读结果回填 debug bodies
                if (meshSDFCount > 0)
                {
                    m_MeshSDFDebugBodies.reserve(meshSDFCount);
                    for (uint32_t i = 0; i < meshSDFCount && i < MAX_MESH_SDF_BODIES; ++i)
                    {
                        const auto&      meta = staging[i];
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
            }
            m_SDFMetaReadback->CopyFrom(m_MeshSDFMetaBuffer, MAX_MESH_SDF_BODIES * sizeof(GPUMeshSDFData), 0);
        }

        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_VulkanResources->TimestampPool, queryBase + 1);
        m_VulkanResources->TimestampWritten[frameIndex] = true;
        PerformanceMonitor::Get().SetFluidActive(true);
    }
#endif // ENGINE_ENABLE_VULKAN

} // namespace Engine
