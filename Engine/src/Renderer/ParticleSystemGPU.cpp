#include "engpch.h"
#include "Renderer/ParticleSystemGPU.h"
#include "Renderer/SPHCommon.h"
#include "Renderer/SPHKernelMath.h"
#include "Core/Log.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/RendererAPI.h"
#include "Renderer/RendererCapabilities.h"
#include "Scene/Components.h"

#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

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
#include "Platform/CUDA/CudaParticlePipeline.h"
#include "Platform/CUDA/CudaParticleTypes.h"
#include "Platform/CUDA/CudaSPHPipeline.h"
#include "Platform/CUDA/CudaErrorHandling.h"
#include "Platform/CUDA/CudaTimingHelper.h"
#endif

namespace Engine
{

    // =========================================================================
    // CudaImpl — CUDA-GL interop + 管线分派（CUDA 移除前的实现恢复）
    // =========================================================================
#ifdef ENGINE_ENABLE_CUDA
    struct ParticleSystemGPU::CudaImpl
    {
        InteropBackend RequestedInteropBackend = InteropBackend::CudaGL;
        InteropBackend ActiveInteropBackend    = InteropBackend::CudaGL;

        Scope<CudaGLInteropContext> GLInterop;
        bool                        UseCudaPath   = false;
        bool                        InitAttempted = false;

        int SlotParticle  = -1;
        int SlotDeadList  = -1;
        int SlotAliveList = -1;
        int SlotCounter   = -1;
        int SlotIndirect  = -1;

        CudaTimingHelper Timing;
        void*            SPHCtx = nullptr;

        ~CudaImpl()
        {
            if (SPHCtx)
                CudaInterop::DestroySPHContext(SPHCtx);
            Timing.Destroy();
        }

        void* GetStream() const
        {
            if (GLInterop)
                return GLInterop->GetStream();
            return nullptr;
        }

        void* GetMappedPointer(int slot) const
        {
            if (GLInterop)
                return GLInterop->GetMappedPointer(slot);
            return nullptr;
        }

        bool MapAll()
        {
            if (GLInterop)
                return GLInterop->MapAll();
            return false;
        }

        void UnmapAll()
        {
            if (GLInterop)
                GLInterop->UnmapAll();
        }
    };
#else
    struct ParticleSystemGPU::CudaImpl
    {
    }; // Empty stub when CUDA disabled
#endif

    namespace
    {
        // SPH warm-up: 新粒子在此时间内 SPH 压力力从 0 渐入到 100%
        // 防止爆发 (burst) 发射时密度冲击导致位置突变/闪烁
        constexpr float SPH_WARMUP_TIME = 0.08f; // 秒（约 5 帧 @ 60 FPS）

        bool ContainsToken(const char* str, const char* token)
        {
            return str && token && std::strstr(str, token) != nullptr;
        }

        struct ParticleABRuntimeState
        {
            bool Initialized = false;

            bool EnvForceGLValid         = false;
            bool EnvForceGLValue         = false;
            bool EnvDisableReadbackValid = false;
            bool EnvDisableReadbackValue = false;

            bool UIForceGLValue           = false;
            bool UIDisableReadbackValue   = false;
            bool UIForceGLTouched         = false;
            bool UIDisableReadbackTouched = false;

            // Benchmark A/B 帧交替：env ENGINE_PARTICLE_AB_AUTO_ALTERNATE=1 启用，每帧按奇偶切换
            // forceGL——偶数帧跑 CUDA，奇数帧跑 GL Compute，同屏两路 ms 形成实时对比。
            bool     AutoAlternateEnabled  = false;
            uint64_t AutoAlternateFrameIdx = 0;

            bool                              LastLogValid        = false;
            bool                              LastForceGL         = false;
            bool                              LastDisableReadback = false;
            ParticleSystemGPU::ABConfigSource LastForceSource     = ParticleSystemGPU::ABConfigSource::Default;
            ParticleSystemGPU::ABConfigSource LastDisableSource   = ParticleSystemGPU::ABConfigSource::Default;
        };

        ParticleABRuntimeState& GetParticleABRuntimeState()
        {
            static ParticleABRuntimeState state;
            return state;
        }

        std::string NormalizeBoolToken(const char* raw)
        {
            if (!raw)
                return {};

            std::string  token(raw);
            const size_t begin = token.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos)
                return {};
            const size_t end = token.find_last_not_of(" \t\r\n");
            token            = token.substr(begin, end - begin + 1);
            for (char& ch : token)
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            return token;
        }

        bool TryParseBoolEnv(const char* raw, bool& outValue)
        {
            const std::string token = NormalizeBoolToken(raw);
            if (token == "1" || token == "true" || token == "on")
            {
                outValue = true;
                return true;
            }
            if (token == "0" || token == "false" || token == "off")
            {
                outValue = false;
                return true;
            }
            return false;
        }

        const char* ABSourceToString(ParticleSystemGPU::ABConfigSource source)
        {
            switch (source)
            {
            case ParticleSystemGPU::ABConfigSource::Env:
                return "ENV";
            case ParticleSystemGPU::ABConfigSource::UI:
                return "UI";
            case ParticleSystemGPU::ABConfigSource::Default:
            default:
                return "Default";
            }
        }

        ParticleSystemGPU::ABConfigSnapshot ResolveParticleABConfigSnapshot()
        {
            auto& state = GetParticleABRuntimeState();

            ParticleSystemGPU::ABConfigSnapshot snapshot{};
            snapshot.ForceGLLockedByEnv         = state.EnvForceGLValid;
            snapshot.DisableReadbackLockedByEnv = state.EnvDisableReadbackValid;

            if (state.EnvForceGLValid)
            {
                snapshot.ForceGL       = state.EnvForceGLValue;
                snapshot.ForceGLSource = ParticleSystemGPU::ABConfigSource::Env;
            }
            else
            {
                snapshot.ForceGL       = state.UIForceGLValue;
                snapshot.ForceGLSource = state.UIForceGLTouched ? ParticleSystemGPU::ABConfigSource::UI
                                                                : ParticleSystemGPU::ABConfigSource::Default;
            }

            if (state.EnvDisableReadbackValid)
            {
                snapshot.DisableCounterReadback = state.EnvDisableReadbackValue;
                snapshot.DisableReadbackSource  = ParticleSystemGPU::ABConfigSource::Env;
            }
            else
            {
                snapshot.DisableCounterReadback = state.UIDisableReadbackValue;
                snapshot.DisableReadbackSource  = state.UIDisableReadbackTouched
                                                      ? ParticleSystemGPU::ABConfigSource::UI
                                                      : ParticleSystemGPU::ABConfigSource::Default;
            }

            return snapshot;
        }

        void LogParticleABSummaryIfChanged(const char* reason)
        {
            auto& state    = GetParticleABRuntimeState();
            auto  snapshot = ResolveParticleABConfigSnapshot();

            // Auto-alternate 模式会按奇偶帧持续切换 forceGL，禁用变更日志避免 stdout 刷屏
            // （init 时的 "Auto-alternate enabled" INFO 仍会打一次）。
            if (state.AutoAlternateEnabled)
                return;

            if (state.LastLogValid && state.LastForceGL == snapshot.ForceGL &&
                state.LastDisableReadback == snapshot.DisableCounterReadback &&
                state.LastForceSource == snapshot.ForceGLSource &&
                state.LastDisableSource == snapshot.DisableReadbackSource)
            {
                return;
            }

            ENGINE_CORE_INFO("[Particle][AB] {} forceGL={} ({}) disableReadback={} ({})", reason,
                             snapshot.ForceGL ? 1 : 0, ABSourceToString(snapshot.ForceGLSource),
                             snapshot.DisableCounterReadback ? 1 : 0, ABSourceToString(snapshot.DisableReadbackSource));

            state.LastLogValid        = true;
            state.LastForceGL         = snapshot.ForceGL;
            state.LastDisableReadback = snapshot.DisableCounterReadback;
            state.LastForceSource     = snapshot.ForceGLSource;
            state.LastDisableSource   = snapshot.DisableReadbackSource;
        }

        void InitParticleABFromEnvIfNeeded()
        {
            auto& state = GetParticleABRuntimeState();
            if (state.Initialized)
                return;
            state.Initialized = true;

            if (const char* rawForceGL = std::getenv("ENGINE_PARTICLE_AB_FORCE_GL"))
            {
                bool value = false;
                if (TryParseBoolEnv(rawForceGL, value))
                {
                    state.EnvForceGLValid = true;
                    state.EnvForceGLValue = value;
                }
                else
                {
                    ENGINE_CORE_WARN("[Particle][AB] Invalid ENGINE_PARTICLE_AB_FORCE_GL='{}', ignored.", rawForceGL);
                }
            }

            if (const char* rawDisableReadback = std::getenv("ENGINE_PARTICLE_AB_DISABLE_READBACK"))
            {
                bool value = false;
                if (TryParseBoolEnv(rawDisableReadback, value))
                {
                    state.EnvDisableReadbackValid = true;
                    state.EnvDisableReadbackValue = value;
                }
                else
                {
                    ENGINE_CORE_WARN("[Particle][AB] Invalid ENGINE_PARTICLE_AB_DISABLE_READBACK='{}', ignored.",
                                     rawDisableReadback);
                }
            }

            if (const char* rawAutoAlt = std::getenv("ENGINE_PARTICLE_AB_AUTO_ALTERNATE"))
            {
                bool value = false;
                if (TryParseBoolEnv(rawAutoAlt, value) && value)
                {
                    state.AutoAlternateEnabled = true;
                    ENGINE_CORE_INFO(
                        "[Particle][AB] Auto-alternate enabled (even frames: CUDA, odd frames: GL compute).");
                }
            }

            LogParticleABSummaryIfChanged("Init");
        }

        // 每帧轮换 forceGL 用于 Benchmark A/B 对比（不覆盖 env lock 与 UI 手动操作）。
        // 调用方：ParticleSystemGPU::Update 开头，在 GetABConfigSnapshot 之前。
        void TickParticleAutoAlternateIfNeeded()
        {
            auto& state = GetParticleABRuntimeState();
            if (!state.AutoAlternateEnabled)
                return;

            // 奇数帧 forceGL=true → GL compute，偶数帧 forceGL=false → CUDA 路径
            // （与 Init 时 OwneredInterop 设置对应 CUDA 真启用）
            bool oddFrame          = (state.AutoAlternateFrameIdx & 1ULL) != 0;
            state.UIForceGLTouched = true;
            state.UIForceGLValue   = oddFrame;
            state.AutoAlternateFrameIdx++;
        }
    } // namespace

    // Must match GLSL struct layout: 5 x vec4 = 80 bytes
    struct GPUParticleData
    {
        glm::vec4 posAndLife;
        glm::vec4 velAndMaxLife;
        glm::vec4 startColor;
        glm::vec4 endColor;
        glm::vec4 params; // x=sizeStart, y=sizeEnd, z=density(SPH), w=pressure(SPH)
    };

    // Counter buffer layout: 4 x uint32 = 16 bytes
    struct CounterData
    {
        uint32_t deadCount;
        uint32_t aliveCount;
        uint32_t emitCount;
        uint32_t pad;
    };

    // DrawArraysIndirectCommand: 4 x uint32 = 16 bytes
    struct IndirectDrawCommand
    {
        uint32_t count;
        uint32_t instanceCount;
        uint32_t first;
        uint32_t baseInstance;
    };

#ifdef ENGINE_ENABLE_CUDA
    // 校验 C++/GLSL/CUDA 三端数据布局一致性
    static_assert(sizeof(GPUParticleData) == sizeof(CudaInterop::GPUParticle),
                  "GPUParticleData size mismatch between C++ and CUDA");
    static_assert(sizeof(CounterData) == sizeof(CudaInterop::CounterData),
                  "CounterData size mismatch between C++ and CUDA");
    static_assert(sizeof(IndirectDrawCommand) == sizeof(CudaInterop::IndirectDrawCommand),
                  "IndirectDrawCommand size mismatch between C++ and CUDA");
    static_assert(sizeof(GPURigidBodyData) == sizeof(CudaInterop::RigidBodyData),
                  "GPURigidBodyData size mismatch between C++ and CUDA");
#endif

#ifdef ENGINE_ENABLE_VULKAN
    namespace
    {
        // std140 UBO 镜像 — 与 particle_emit.glsl 中 EmitParams 块逐字节对应
        struct alignas(16) ParticleEmitParamsUBO
        {
            glm::vec4 EmitterPosAndAngle; // xyz=EmitterPos, w=EmitAngle(radians)
            glm::vec4 EmitDirAndSpeedMin; // xyz=EmitDirection, w=SpeedMin
            glm::vec4 LifeAndSpeedMax;    // x=LifeMin, y=LifeMax, z=SpeedMax, w=unused
            glm::vec4 SizeStartEnd;       // x=SizeStart, y=SizeEnd, z=unused, w=unused
            glm::vec4 StartColor;
            glm::vec4 EndColor;
        };
        static_assert(sizeof(ParticleEmitParamsUBO) == 96, "ParticleEmitParamsUBO must be std140-aligned 96 bytes");

        // std140 UBO 镜像 — 与 particle_simulate.glsl 中 SimParams 块对应
        struct alignas(16) ParticleSimParamsUBO
        {
            glm::vec4 GravityAndDamping; // xyz=Gravity, w=Damping
        };
        static_assert(sizeof(ParticleSimParamsUBO) == 16, "ParticleSimParamsUBO must be std140-aligned 16 bytes");

        // Push constant 布局：与 particle_emit.glsl 中 PushConstants 对应
        struct ParticleEmitPC
        {
            uint32_t EmitCount;
            uint32_t MaxParticles;
            float    Time;
            uint32_t Seed;
        };
        static_assert(sizeof(ParticleEmitPC) == 16, "EmitPC must be 16 bytes");

        // Push constant 布局：与 particle_compact.glsl 中 PushConstants 对应
        struct ParticleCompactPC
        {
            uint32_t MaxParticles;
        };

        // Push constant 布局：与 particle_simulate.glsl 中 PushConstants 对应
        struct ParticleSimulatePC
        {
            float    DeltaTime;
            uint32_t MaxParticles;
            uint32_t Flags;
        };
        static_assert(sizeof(ParticleSimulatePC) == 12, "SimulatePC must be 12 bytes");

        // Push constant 布局：与 particle_render_args.glsl 中 PushConstants 对应
        struct ParticleRenderArgsPC
        {
            uint32_t MaxParticles;
        };

        // ====================================================================
        // SPH 路径共享 UBO（binding=12）+ push constant 镜像
        // density + force 两 shader 用同一布局；详见 sph_density.glsl / sph_force.glsl Vulkan 分支。
        // ====================================================================
        struct alignas(16) ParticleSPHParamsUBO
        {
            glm::vec4 GravityAndSmoothingRadius;
            glm::vec4 MassDensityGasViscosity;
            glm::vec4 GridParams;
            glm::vec4 BoundaryParams;
            glm::vec4 SDFCounts;
        };
        static_assert(sizeof(ParticleSPHParamsUBO) == 80, "ParticleSPHParamsUBO must be std140-aligned 80 bytes");

        struct ParticleSPHPushConstants
        {
            uint32_t AliveCount;
            float    DeltaTime;
            uint32_t UsePredictedPos;
        };
        static_assert(sizeof(ParticleSPHPushConstants) == 12, "ParticleSPHPushConstants must be 12 bytes");

        // Binding 常量（与 shader layout 一致）
        constexpr uint32_t PARTICLE_EMIT_UBO_BINDING = 5;
        constexpr uint32_t PARTICLE_SIM_UBO_BINDING  = 6;
        constexpr uint32_t PARTICLE_SPH_UBO_BINDING  = 12;
    } // namespace

    // ============================================================
    // Vulkan 资源（Pimpl）— 仅 ENGINE_ENABLE_VULKAN 时存在
    // ============================================================
    struct ParticleSystemGPU::VulkanResources
    {
        bool                           Initialized = false;
        VkDevice                       Device      = VK_NULL_HANDLE;
        Ref<VulkanDescriptorSetLayout> EmitLayout;
        Ref<VulkanDescriptorSetLayout> CompactLayout;
        Ref<VulkanDescriptorSetLayout> SimulateLayout;
        Ref<VulkanDescriptorSetLayout> RenderArgsLayout;
        VulkanComputePipelineHandle    EmitPipeline{};
        VulkanComputePipelineHandle    CompactPipeline{};
        VulkanComputePipelineHandle    SimulatePipeline{};
        VulkanComputePipelineHandle    RenderArgsPipeline{};
        Ref<VulkanDescriptorPool>      Pool;

        // ---- SPH 2 pipeline（WCSPH path：density + force）----
        bool                           SPHInitialized = false;
        Ref<VulkanDescriptorSetLayout> SPHDensityLayout;
        Ref<VulkanDescriptorSetLayout> SPHForceLayout;
        VulkanComputePipelineHandle    SPHDensityPipeline{};
        VulkanComputePipelineHandle    SPHForcePipeline{};
    };
#else
    struct ParticleSystemGPU::VulkanResources
    {
        // Empty stub on non-Vulkan builds (header still references the type)
    };
#endif

    ParticleSystemGPU::ParticleSystemGPU(uint32_t maxParticles)
        : m_MaxParticles(maxParticles), m_CudaImpl(CreateScope<CudaImpl>())
    {
    }

    ParticleSystemGPU::~ParticleSystemGPU()
    {
        // CudaImpl 析构函数处理 CUDA 资源清理
        // m_Readback via Ref<GPUAsyncReadback> auto-destructs
#ifdef ENGINE_ENABLE_VULKAN
        DestroyVulkanComputeResources();
#endif
    }

    ParticleSystemGPU::ABConfigSnapshot ParticleSystemGPU::GetABConfigSnapshot()
    {
        InitParticleABFromEnvIfNeeded();
        return ResolveParticleABConfigSnapshot();
    }

    void ParticleSystemGPU::SetABConfigFromUI(bool forceGL, bool disableCounterReadback)
    {
        InitParticleABFromEnvIfNeeded();
        auto& state = GetParticleABRuntimeState();

        bool changed = false;

        if (!state.EnvForceGLValid)
        {
            if (!state.UIForceGLTouched)
            {
                state.UIForceGLTouched = true;
                changed                = true;
            }
            if (state.UIForceGLValue != forceGL)
            {
                state.UIForceGLValue = forceGL;
                changed              = true;
            }
        }

        if (!state.EnvDisableReadbackValid)
        {
            if (!state.UIDisableReadbackTouched)
            {
                state.UIDisableReadbackTouched = true;
                changed                        = true;
            }
            if (state.UIDisableReadbackValue != disableCounterReadback)
            {
                state.UIDisableReadbackValue = disableCounterReadback;
                changed                      = true;
            }
        }

        if (changed)
            LogParticleABSummaryIfChanged("UI update");
    }

    const char* ParticleSystemGPU::ABConfigSourceLabel(ABConfigSource source)
    {
        return ABSourceToString(source);
    }

    ParticleSystemGPU::InteropBackend ParticleSystemGPU::GetRequestedInteropBackend()
    {
        return InteropBackend::CudaGL;
    }

    const char* ParticleSystemGPU::InteropBackendLabel(InteropBackend backend)
    {
        switch (backend)
        {
        case InteropBackend::CudaGL:
        default:
            return "CudaGL";
        }
    }

    void ParticleSystemGPU::Init()
    {
        if (m_Initialized)
            return;

        // 初始化并打印一次 AB 开关摘要（环境变量/默认值）
        (void)GetABConfigSnapshot();

        // Load compute shaders
        m_EmitShader       = Shader::Create("assets/shaders/particle_emit.glsl");
        m_SimulateShader   = Shader::Create("assets/shaders/particle_simulate.glsl");
        m_CompactShader    = Shader::Create("assets/shaders/particle_compact.glsl");
        m_RenderArgsShader = Shader::Create("assets/shaders/particle_render_args.glsl");
        m_BillboardShader  = Shader::Create("assets/shaders/particle_billboard.glsl");

        // SPH shaders (loaded eagerly, only dispatched when SPHEnabled)
        m_SPHShaders = SPHShaderSet::Load();

        // Allocate particle pool — MUST be zero-initialized so all particles
        // start with life=0.0 (properly dead). Undefined buffer data may contain
        // NaN or positive life values, causing simulate to treat uninitialized
        // particles as alive and permanently corrupting the dead/alive counters.
        // GPU-only immutable storage: after init, only GPU reads/writes this buffer.
        uint32_t             particleSize = sizeof(GPUParticleData); // 80 bytes
        uint32_t             totalBytes   = m_MaxParticles * particleSize;
        std::vector<uint8_t> zeroData(totalBytes, 0);
        m_ParticleBuffer = ShaderStorageBuffer::CreateGPUDynamic(zeroData.data(), totalBytes, 0);

        // Fill dead list with indices [0, 1, 2, ..., MAX-1]
        std::vector<uint32_t> deadIndices(m_MaxParticles);
        for (uint32_t i = 0; i < m_MaxParticles; i++)
            deadIndices[i] = i;
        m_DeadList = ShaderStorageBuffer::CreateGPUDynamic(deadIndices.data(), m_MaxParticles * sizeof(uint32_t), 1);

        // Alive list (empty at start)
        m_AliveList = ShaderStorageBuffer::CreateGPUDynamic(m_MaxParticles * sizeof(uint32_t), 2);

        // Counter buffer: deadCount=MAX, aliveCount=0, emitCount=0, pad=0
        // Kept as DYNAMIC — CPU writes aliveCount/emitCount every frame
        CounterData counters{m_MaxParticles, 0, 0, 0};
        m_CounterBuffer = ShaderStorageBuffer::Create(&counters, sizeof(CounterData), 3);

        // Indirect draw args: count=6, instanceCount=0, first=0, baseInstance=0
        IndirectDrawCommand cmd{6, 0, 0, 0};
        m_IndirectArgs = ShaderStorageBuffer::CreateGPUDynamic(&cmd, sizeof(IndirectDrawCommand), 4);

        // Empty VAO for billboard rendering
        m_EmptyVAO = VertexArray::Create();

        // 异步回读缓冲：用于避免 glGetBufferSubData 的同步阻塞
        m_Readback = GPUAsyncReadback::Create(sizeof(CounterData));

        // Vulkan 路径需要 UBO 上传 emitter / simulate 大块参数（OpenGL 路径不使用，
        // 但 OpenGL 4.3 explicit binding 兼容 — 即使 OpenGL 路径下创建也无害）
#ifdef ENGINE_ENABLE_VULKAN
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
        {
            m_EmitParamsUBO = UniformBuffer::Create(sizeof(ParticleEmitParamsUBO), PARTICLE_EMIT_UBO_BINDING);
            m_SimParamsUBO  = UniformBuffer::Create(sizeof(ParticleSimParamsUBO), PARTICLE_SIM_UBO_BINDING);
            m_SPHParamsUBO  = UniformBuffer::Create(sizeof(ParticleSPHParamsUBO), PARTICLE_SPH_UBO_BINDING);
        }
#endif

        auto& caps = RendererCapabilities::Get();

        // VMware SVGA has known instability with advanced compute/indirect paths.
        bool vmwareDriver =
            ContainsToken(caps.VendorString.c_str(), "VMware") || ContainsToken(caps.RendererString.c_str(), "SVGA3D");
        m_VMwareCompatMode = vmwareDriver;

        const char* forceDirect    = std::getenv("ENGINE_PARTICLE_DIRECT_DRAW");
        bool        envForceDirect = forceDirect && forceDirect[0] == '1';

        m_UseIndirectDraw = !(vmwareDriver || envForceDirect);
        if (!m_UseIndirectDraw)
        {
            ENGINE_WARN("[Particle] Using direct instanced draw fallback (VMware compatibility mode).");
        }

        const char* allowSPHEnv      = std::getenv("ENGINE_ENABLE_SPH_ON_VMWARE");
        bool        allowSPHOnVMware = allowSPHEnv && allowSPHEnv[0] == '1';
        m_DisableSPHOnDriver         = vmwareDriver && !allowSPHOnVMware;
        if (m_DisableSPHOnDriver)
        {
            ENGINE_WARN("[Particle] SPH/PCISPH disabled on VMware for stability. Set ENGINE_ENABLE_SPH_ON_VMWARE=1 to "
                        "force-enable.");
        }

#ifdef ENGINE_ENABLE_CUDA
        // ---- CUDA compute sidecar 初始化 ----
        m_CudaImpl->RequestedInteropBackend = GetRequestedInteropBackend();

        if (!m_CudaImpl->InitAttempted)
        {
            m_CudaImpl->InitAttempted = true;

            if (m_CudaImpl->RequestedInteropBackend == InteropBackend::CudaGL && !CudaInterop::IsCudaPoisoned())
            {
                m_CudaImpl->GLInterop = CreateScope<CudaGLInteropContext>();
                m_CudaImpl->SlotParticle =
                    m_CudaImpl->GLInterop->RegisterBuffer(m_ParticleBuffer->GetRendererID(), "ParticleBuffer");
                m_CudaImpl->SlotDeadList =
                    m_CudaImpl->GLInterop->RegisterBuffer(m_DeadList->GetRendererID(), "DeadList");
                m_CudaImpl->SlotAliveList =
                    m_CudaImpl->GLInterop->RegisterBuffer(m_AliveList->GetRendererID(), "AliveList");
                m_CudaImpl->SlotCounter =
                    m_CudaImpl->GLInterop->RegisterBuffer(m_CounterBuffer->GetRendererID(), "CounterBuffer");
                m_CudaImpl->SlotIndirect =
                    m_CudaImpl->GLInterop->RegisterBuffer(m_IndirectArgs->GetRendererID(), "IndirectArgs");

                if (m_CudaImpl->SlotParticle >= 0 && m_CudaImpl->SlotDeadList >= 0 && m_CudaImpl->SlotAliveList >= 0 &&
                    m_CudaImpl->SlotCounter >= 0 && m_CudaImpl->SlotIndirect >= 0)
                {
                    m_CudaImpl->UseCudaPath = true;
                    m_CudaImpl->Timing.Init();
                    m_CudaImpl->SPHCtx = CudaInterop::CreateSPHContext(m_MaxParticles, 64, MAX_RIGID_BODIES);
                    ENGINE_CORE_INFO("[CUDA] Particle初始化成功 ({0} slots)", m_CudaImpl->GLInterop->GetSlotCount());
                }
                else
                {
                    ENGINE_CORE_WARN("[CUDA] GL buffer 注册失败，回退到 GL Compute 路径");
                    m_CudaImpl->GLInterop.reset();
                }
            }
        }
#endif

        m_Initialized = true;
    }

    void ParticleSystemGPU::InitSPH(float smoothingRadius)
    {
        if (m_SPHInitialized)
            return;

        // Grid cell size = smoothing radius：27-cell stencil 覆盖搜索半径 h，
        // 距离过滤保证结果与 2h 版本一致，扫描体积从 (6h)³ 收紧到 (3h)³。
        float    cellSize = smoothingRadius;
        uint32_t gridSize = (m_MaxParticles <= 8000) ? 16 : (m_MaxParticles <= 30000) ? 32 : 64;

        m_Grid.Init(m_MaxParticles, gridSize, cellSize);
        m_SPHInitialized = true;
    }

    void ParticleSystemGPU::InitPCISPH()
    {
        if (m_PCISPHInitialized)
            return;
        // PCISPHData: 3 × vec4 = 48 bytes per particle
        m_PCISPHBuffer      = ShaderStorageBuffer::CreateGPUOnly(m_MaxParticles * 48, 1);
        m_PCISPHInitialized = true;
    }

    void ParticleSystemGPU::InitRigidBodyBuffer()
    {
        if (m_RigidBodyBuffer)
            return;
        m_RigidBodyBuffer = ShaderStorageBuffer::Create(MAX_RIGID_BODIES * sizeof(GPURigidBodyData), 3);
    }

    void ParticleSystemGPU::Update(float                           dt,
                                   const glm::vec3&                emitterPos,
                                   const ParticleEmitterComponent& emitter,
                                   entt::registry*                 registry)
    {
        if (!m_Initialized)
            return;

#ifdef ENGINE_ENABLE_VULKAN
        // Vulkan 路径分派 — 非 SPH 路径已迁，SPH 暂跳过（Commit C 落地）
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
        {
            UpdateVulkan(dt, emitterPos, emitter, registry);
            return;
        }
#endif

        TickParticleAutoAlternateIfNeeded();
        const ABConfigSnapshot abConfig = GetABConfigSnapshot();

        float counterReadbackCpuMs = 0.0f;

        const bool sphEnabled = emitter.SPH.Enabled && !m_DisableSPHOnDriver;
        if (emitter.SPH.Enabled && m_DisableSPHOnDriver && !m_SPHDisableLogged)
        {
            ENGINE_WARN("[Particle] SPH component detected but runtime SPH is disabled in VMware compatibility mode.");
            m_SPHDisableLogged = true;
        }

        // ---- CPU-side: reset aliveCount, set emitCount ----
        uint32_t zero = 0;
        m_CounterBuffer->SetData(&zero, sizeof(uint32_t), 4); // aliveCount = 0

        // Compute how many particles to emit (clamp dt to prevent first-frame spike)
        float clampedDt = std::min(dt, 0.05f);
        m_EmitAccumulator += emitter.EmitRate * clampedDt;
        uint32_t emitCount = static_cast<uint32_t>(m_EmitAccumulator);
        m_EmitAccumulator -= static_cast<float>(emitCount);

        // Add burst (user-triggered + collision-triggered)
        int totalBurst = emitter.PendingBurst + emitter.CollisionBurstCount;
        if (totalBurst > 0)
            emitCount += static_cast<uint32_t>(totalBurst);

        // Clamp to MaxParticles — prevent shader atomic underflow
        emitCount = std::min(emitCount, m_MaxParticles);

        m_CounterBuffer->SetData(&emitCount, sizeof(uint32_t), 8); // emitCount

        // GL Compute path（CUDA 路径在此之上，若成功则跳过）
        PerformanceMonitor::Get().GetParticleComputeGPUTimer().Begin();

#ifdef ENGINE_ENABLE_CUDA
        bool cudaRan = false;

        // ---- 全部 4 个 pass 共用单次 Map/Unmap 作用域（emit → SPH → simulate → render args）----
        // 原实现 emit 与 SPH/simulate 各持一对独立 MapAll/UnmapAll，每帧两次 GPU 所有权转移；
        // 同一 stream 内 kernel 天然有序，GL 端仅在 Unmap 之后消费结果，合并语义安全，
        // 且把 WDDM 隐式 barrier 的暴露面减半。
        if (!abConfig.ForceGL && m_CudaImpl->UseCudaPath &&
            m_CudaImpl->ActiveInteropBackend == InteropBackend::CudaGL && !CudaInterop::IsCudaPoisoned())
        {
            if (m_CudaImpl->MapAll())
            {
                m_TotalTime += dt;

                cudaStream_t strm = static_cast<cudaStream_t>(m_CudaImpl->GetStream());
                m_CudaImpl->Timing.RecordStart(strm);
                void* devParticles = m_CudaImpl->GetMappedPointer(m_CudaImpl->SlotParticle);
                void* devDeadList  = m_CudaImpl->GetMappedPointer(m_CudaImpl->SlotDeadList);
                void* devAliveList = m_CudaImpl->GetMappedPointer(m_CudaImpl->SlotAliveList);
                void* devCounter   = m_CudaImpl->GetMappedPointer(m_CudaImpl->SlotCounter);
                void* devIndirect  = m_CudaImpl->GetMappedPointer(m_CudaImpl->SlotIndirect);

                // ---- Pass 1: Emit ----
                // Clear aliveCount（CUDA kernel 中 atomicAdd 会累加）
                uint32_t zeroAlive = 0;
                cudaMemcpyAsync(static_cast<uint8_t*>(devCounter) + offsetof(CounterData, aliveCount), &zeroAlive,
                                sizeof(uint32_t), cudaMemcpyHostToDevice, strm);

                if (emitCount > 0)
                {
                    CudaInterop::EmitParams ep{};
                    ep.emitterPos[0]    = emitterPos.x;
                    ep.emitterPos[1]    = emitterPos.y;
                    ep.emitterPos[2]    = emitterPos.z;
                    ep.emitDirection[0] = emitter.EmitDirection.x;
                    ep.emitDirection[1] = emitter.EmitDirection.y;
                    ep.emitDirection[2] = emitter.EmitDirection.z;
                    ep.emitAngle        = glm::radians(emitter.EmitAngle);
                    ep.lifeMin          = std::max(emitter.LifeMin, 1e-6f);
                    ep.lifeMax          = std::max(emitter.LifeMax, ep.lifeMin);
                    ep.speedMin         = emitter.SpeedMin;
                    ep.speedMax         = emitter.SpeedMax;
                    ep.sizeStart        = emitter.SizeStart;
                    ep.sizeEnd          = emitter.SizeEnd;
                    ep.startColor[0]    = emitter.ColorStart.r;
                    ep.startColor[1]    = emitter.ColorStart.g;
                    ep.startColor[2]    = emitter.ColorStart.b;
                    ep.startColor[3]    = emitter.ColorStart.a;
                    ep.endColor[0]      = emitter.ColorEnd.r;
                    ep.endColor[1]      = emitter.ColorEnd.g;
                    ep.endColor[2]      = emitter.ColorEnd.b;
                    ep.endColor[3]      = emitter.ColorEnd.a;
                    ep.time             = m_TotalTime;
                    ep.maxParticles     = m_MaxParticles;
                    ep.emitCount        = emitCount;
                    CudaInterop::LaunchEmit(devParticles, devDeadList, devCounter, ep, strm);
                }

                // ---- Pass 2: SPH（仅 sphEnabled && 有存活粒子时跑）----
                // SPH 路径下重力被 PCISPH 内核处理，Simulate 传 gravity=0；WCSPH/non-SPH
                // 走完整 emitter.Gravity。SPH 使用上一帧 m_LastAliveCount 做 dispatch（与 GL 一致延迟机制）。
                if (sphEnabled && m_LastAliveCount > 0)
                {
                    const SPHKernelParams kp       = SPHKernelParams::Compute(emitter.SPH.SmoothingRadius);
                    const float           cellSize = m_Grid.GetCellSize();
                    const int             gridSize = static_cast<int>(m_Grid.GetGridSize());

                    CudaInterop::SPHParams sphP{};
                    sphP.smoothingRadius = emitter.SPH.SmoothingRadius;
                    sphP.poly6Coeff      = kp.poly6Coeff;
                    sphP.spikyCoeff      = kp.spikyCoeff;
                    sphP.particleMass    = emitter.SPH.ParticleMass;
                    sphP.restDensity     = emitter.SPH.RestDensity;
                    sphP.gasConstant     = emitter.SPH.GasConstant;
                    sphP.viscosity       = emitter.SPH.Viscosity;
                    sphP.surfaceTension  = emitter.SPH.SurfaceTension;
                    sphP.deltaTime       = clampedDt;
                    sphP.gridSize        = gridSize;
                    sphP.cellSize        = cellSize;
                    sphP.aliveCount      = static_cast<int>(m_LastAliveCount);
                    sphP.gravity[0]      = emitter.Gravity.x;
                    sphP.gravity[1]      = emitter.Gravity.y;
                    sphP.gravity[2]      = emitter.Gravity.z;
                    sphP.warmupTime      = SPH_WARMUP_TIME;

                    // 收集刚体数据并上传到 SPHCtx->d_rigidBody，PCISPH/WCSPH 共用
                    std::vector<GPURigidBodyData> rigidBodies;
                    if (emitter.SPH.RigidBodyCoupling && registry)
                    {
                        rigidBodies = CollectRigidBodies(registry, MAX_RIGID_BODIES,
                                                         RigidBodyUploadFilter::RequireRigidBodyComponent);
                        if (!rigidBodies.empty())
                            CudaInterop::SPHUploadRigidBodies(m_CudaImpl->SPHCtx, rigidBodies.data(),
                                                              static_cast<uint32_t>(rigidBodies.size()));
                    }
                    // static_assert 在文件头已校验 GPURigidBodyData 与 CudaInterop::RigidBodyData 布局一致
                    const uint32_t rigidBodyCount = static_cast<uint32_t>(rigidBodies.size());

                    // Pass 2a: Grid Build (CUB ExclusiveSum 替代 GL 3-pass prefix sum)
                    // 传存活槽→池索引映射表：CUDA SPH 内核与 GL 一致地经 alive list
                    // 间接寻址，死粒子不再被误算、池后段活粒子不再被漏算
                    CudaInterop::LaunchSPHGridBuild(m_CudaImpl->SPHCtx, devParticles,
                                                    static_cast<uint32_t>(m_LastAliveCount), gridSize, cellSize, strm,
                                                    false, devAliveList);

                    if (emitter.SPH.PCISPHEnabled)
                    {
                        // Pass 2b: PCISPH path
                        CudaInterop::PCISPHIterParams ip{};
                        ip.pcisphDelta = SPHKernelMath::ComputePCISPHDelta(
                            emitter.SPH.SmoothingRadius, emitter.SPH.ParticleMass, emitter.SPH.RestDensity, clampedDt);
                        ip.boundaryStiffness = emitter.SPH.BoundaryStiffness;
                        ip.boundaryDamping   = emitter.SPH.BoundaryDamping;
                        ip.rigidBodyCount    = static_cast<int>(rigidBodyCount);
                        ip.usePredictedPos   = 0;

                        CudaInterop::LaunchPCISPHInit(m_CudaImpl->SPHCtx, devParticles, sphP, strm, devAliveList);

                        int iterations = std::clamp(emitter.SPH.PCISPHIterations, 1, 8);
                        for (int iter = 0; iter < iterations; ++iter)
                        {
                            ip.usePredictedPos = (iter == 0) ? 0 : 1;
                            CudaInterop::LaunchPCISPHPredict(m_CudaImpl->SPHCtx, devParticles, clampedDt,
                                                             static_cast<int>(m_LastAliveCount), strm, devAliveList);
                            CudaInterop::LaunchPCISPHDensity(m_CudaImpl->SPHCtx, devParticles, sphP, ip, strm,
                                                             devAliveList);
                            CudaInterop::LaunchPCISPHForce(m_CudaImpl->SPHCtx, devParticles, sphP, ip, strm,
                                                           devAliveList);
                        }

                        // 粒子系统的 simulate pass 仍负责位置积分；仅 FluidSystemGPU 使用 PCISPH 预测位置。
                        CudaInterop::LaunchPCISPHApply(m_CudaImpl->SPHCtx, devParticles,
                                                       static_cast<int>(m_LastAliveCount), false, strm, devAliveList);
                    }
                    else
                    {
                        // Pass 2b alt: WCSPH Density + Force
                        CudaInterop::PCISPHIterParams ip{};
                        ip.boundaryStiffness = emitter.SPH.BoundaryStiffness;
                        ip.boundaryDamping   = emitter.SPH.BoundaryDamping;
                        ip.rigidBodyCount    = static_cast<int>(rigidBodyCount);
                        ip.usePredictedPos   = 0;

                        CudaInterop::LaunchSPHDensity(m_CudaImpl->SPHCtx, devParticles, sphP, strm, devAliveList);
                        CudaInterop::LaunchSPHForce(m_CudaImpl->SPHCtx, devParticles, sphP, ip, strm, devAliveList);
                    }
                }

                // ---- Pass 3: Simulate（gravity 调整：PCISPH 内部已处理，传 0）----
                glm::vec3 simGravity = (sphEnabled && emitter.SPH.PCISPHEnabled) ? glm::vec3(0.0f) : emitter.Gravity;
                CudaInterop::SimulateParams sp{};
                sp.deltaTime    = clampedDt;
                sp.gravity[0]   = simGravity.x;
                sp.gravity[1]   = simGravity.y;
                sp.gravity[2]   = simGravity.z;
                sp.damping      = emitter.Damping;
                sp.maxParticles = m_MaxParticles;
                CudaInterop::LaunchSimulate(devParticles, devDeadList, devAliveList, devCounter, sp, strm);

                // ---- Pass 4: Render Args ----
                CudaInterop::LaunchRenderArgs(devCounter, devIndirect, strm);

                m_CudaImpl->Timing.RecordStop(strm);
                m_CudaImpl->UnmapAll();
                cudaRan = !CudaInterop::IsCudaPoisoned();

                if (!cudaRan)
                {
                    ENGINE_WARN("[Particle] CUDA poisoned during compute; falling back to GL compute.");
                }
            }
            else
            {
                ENGINE_WARN("[Particle] CUDA MapAll failed; falling back to GL compute.");
            }
        }

        // 每帧 Swap CUDA ping-pong events，把上一帧已完成的耗时喂给 PerformanceMonitor。
        // 第一帧或前一帧 stop 事件未就绪时 GetPrevElapsedMs 返回 -1，调用方应保留缓
        // 存值——这里直接 set -1 等下次读，避免写入一个陈旧正数而误读为长帧。
        //
        // SwapEvents 仅在 CUDA 帧完整成功（cudaRan=true）时执行——若中途 MapAll 失败，
        // start/stop 事件对不配对，Swap 后会让下一帧 GetPrevElapsedMs 查到孤立 start，
        // 停事件未 record 时 cudaEventQuery 返回 cudaErrorNotReady（经 CudaEventElapsedMs
        // 转译为 -1），仍能干净兜底但不严谨。
        // 保守起见：未成对完成本帧时跳过 Swap，prev 槽位继续持有上一对有效事件。
        if (cudaRan)
        {
            m_CudaImpl->Timing.SwapEvents();
            float ms = m_CudaImpl->Timing.GetPrevElapsedMs();
            if (ms >= 0.0f)
                PerformanceMonitor::Get().SetParticleComputeCudaMs(ms);
        }
        // CUDA Active 标记每帧 set 一次（含 GL fallback 时为 false），避免上一帧 true
        // 状态在中毒后泄漏给 PerformanceMonitor 显示。
        PerformanceMonitor::Get().SetParticleComputeCudaActive(cudaRan);

        if (!cudaRan)
#endif
        {
            // Bind all buffers
            m_ParticleBuffer->Bind(0);
            m_DeadList->Bind(1);
            m_AliveList->Bind(2);
            m_CounterBuffer->Bind(3);
            m_IndirectArgs->Bind(4);

            // ---- Pass 1: Emit ----
            if (emitCount > 0)
            {
                m_EmitShader->Bind();
                m_EmitShader->SetFloat3("u_EmitterPos", emitterPos);
                m_EmitShader->SetFloat3("u_EmitDirection", emitter.EmitDirection);
                m_EmitShader->SetFloat("u_EmitAngle", glm::radians(emitter.EmitAngle));

                // Sanitize LifeMin/LifeMax: ensure positive and LifeMin <= LifeMax
                float safeLifeMin = std::max(emitter.LifeMin, 1e-6f);
                float safeLifeMax = std::max(emitter.LifeMax, safeLifeMin);
                m_EmitShader->SetFloat("u_LifeMin", safeLifeMin);
                m_EmitShader->SetFloat("u_LifeMax", safeLifeMax);
                m_EmitShader->SetFloat("u_SpeedMin", emitter.SpeedMin);
                m_EmitShader->SetFloat("u_SpeedMax", emitter.SpeedMax);
                m_EmitShader->SetFloat("u_SizeStart", emitter.SizeStart);
                m_EmitShader->SetFloat("u_SizeEnd", emitter.SizeEnd);
                m_EmitShader->SetFloat4("u_StartColor", emitter.ColorStart);
                m_EmitShader->SetFloat4("u_EndColor", emitter.ColorEnd);
                m_EmitShader->SetInt("u_MaxParticles", static_cast<int>(m_MaxParticles));

                // Time-based seed for RNG
                m_TotalTime += dt;
                m_EmitShader->SetFloat("u_Time", m_TotalTime);

                uint32_t groups = (emitCount + 63) / 64;
                RenderCommand::DispatchCompute(groups);
                RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);
            }

            // ---- Pass 1.5: Compact alive/dead lists (fresh for SPH) ----
            // emit 后立即重建 alive list，确保 SPH 看到新生粒子
            if (sphEnabled)
            {
                CounterData zero{};
                m_CounterBuffer->SetData(&zero, sizeof(CounterData), 0);
                RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                m_CompactShader->Bind();
                m_CompactShader->SetInt("u_MaxParticles", static_cast<int>(m_MaxParticles));
                uint32_t compactGroups = (m_MaxParticles + 255) / 256;
                RenderCommand::DispatchCompute(compactGroups);
                RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                // 不做同步回读（glGetBufferSubData 会导致 ~28ms GPU pipeline stall）。
                // 使用上一帧异步回读的 m_LastAliveCount，1 帧延迟对 SPH dispatch 可接受。
            }

            // ---- SPH passes (only when SPHEnabled) ----
            if (sphEnabled)
            {
                // Lazy-init spatial hash grid
                if (!m_SPHInitialized)
                    InitSPH(emitter.SPH.SmoothingRadius);

                // compact 已重建 alive list，m_LastAliveCount 是当前帧精确值
                if (m_LastAliveCount > 0)
                {
                    float cellSize = m_Grid.GetCellSize();
                    int   gridSize = static_cast<int>(m_Grid.GetGridSize());

                    // CPU 侧预计算 SPH kernel 常量（避免 GPU 每粒子每邻居重复计算）
                    SPHKernelParams kp = SPHKernelParams::Compute(emitter.SPH.SmoothingRadius);

                    // Pass 2a: Build Spatial Hash Grid
                    m_Grid.Build(m_LastAliveCount);

                    // Pass 2b: SPH Density
                    m_SPHShaders.DensityShader->Bind();
                    m_SPHShaders.DensityShader->SetInt("u_AliveCount", static_cast<int>(m_LastAliveCount));
                    m_SPHShaders.DensityShader->SetFloat("u_SmoothingRadius", emitter.SPH.SmoothingRadius);
                    m_SPHShaders.DensityShader->SetFloat("u_ParticleMass", emitter.SPH.ParticleMass);
                    m_SPHShaders.DensityShader->SetFloat("u_RestDensity", emitter.SPH.RestDensity);
                    m_SPHShaders.DensityShader->SetFloat("u_GasConstant", emitter.SPH.GasConstant);
                    m_SPHShaders.DensityShader->SetInt("u_GridSize", gridSize);
                    m_SPHShaders.DensityShader->SetFloat("u_CellSize", cellSize);
                    m_SPHShaders.DensityShader->SetFloat("u_Poly6Coeff", kp.poly6Coeff);

                    uint32_t sphGroups = (m_LastAliveCount + 255) / 256;
                    RenderCommand::DispatchCompute(sphGroups);
                    RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                    if (emitter.SPH.PCISPHEnabled)
                    {
                        // ---- PCISPH 路径 ----
                        InitPCISPH();

                        uint32_t rigidBodyCount = 0;
                        if (emitter.SPH.RigidBodyCoupling && registry)
                        {
                            InitRigidBodyBuffer();
                            rigidBodyCount =
                                UploadRigidBodiesToBuffer(registry, m_RigidBodyBuffer, MAX_RIGID_BODIES,
                                                          RigidBodyUploadFilter::RequireRigidBodyComponent);
                        }

                        m_PCISPHBuffer->Bind(1);
                        if (m_RigidBodyBuffer)
                            m_RigidBodyBuffer->Bind(3);

                        // PCISPH Init
                        m_SPHShaders.PCISPHInit->Bind();
                        m_SPHShaders.PCISPHInit->SetInt("u_AliveCount", static_cast<int>(m_LastAliveCount));
                        m_SPHShaders.PCISPHInit->SetFloat("u_SmoothingRadius", emitter.SPH.SmoothingRadius);
                        m_SPHShaders.PCISPHInit->SetFloat("u_ParticleMass", emitter.SPH.ParticleMass);
                        m_SPHShaders.PCISPHInit->SetFloat("u_Viscosity", emitter.SPH.Viscosity);
                        m_SPHShaders.PCISPHInit->SetFloat("u_DeltaTime", clampedDt);
                        m_SPHShaders.PCISPHInit->SetInt("u_GridSize", gridSize);
                        m_SPHShaders.PCISPHInit->SetFloat("u_CellSize", cellSize);
                        m_SPHShaders.PCISPHInit->SetFloat3("u_Gravity", emitter.Gravity);
                        m_SPHShaders.PCISPHInit->SetFloat("u_SurfaceTension", emitter.SPH.SurfaceTension);
                        m_SPHShaders.PCISPHInit->SetFloat("u_SpikyCoeff", kp.spikyCoeff);
                        RenderCommand::DispatchCompute(sphGroups);
                        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                        // 循环前一次性设置所有三个 shader 的 uniform（跨 glUseProgram 保持）
                        int iterations = std::clamp(emitter.SPH.PCISPHIterations, 1, 8);

                        m_SPHShaders.PCISPHPredict->Bind();
                        m_SPHShaders.PCISPHPredict->SetInt("u_AliveCount", static_cast<int>(m_LastAliveCount));
                        m_SPHShaders.PCISPHPredict->SetFloat("u_DeltaTime", clampedDt);

                        m_SPHShaders.PCISPHDensity->Bind();
                        m_SPHShaders.PCISPHDensity->SetInt("u_AliveCount", static_cast<int>(m_LastAliveCount));
                        m_SPHShaders.PCISPHDensity->SetFloat("u_SmoothingRadius", emitter.SPH.SmoothingRadius);
                        m_SPHShaders.PCISPHDensity->SetFloat("u_ParticleMass", emitter.SPH.ParticleMass);
                        m_SPHShaders.PCISPHDensity->SetFloat("u_RestDensity", emitter.SPH.RestDensity);
                        m_SPHShaders.PCISPHDensity->SetFloat(
                            "u_PCISPHDelta",
                            SPHKernelMath::ComputePCISPHDelta(emitter.SPH.SmoothingRadius, emitter.SPH.ParticleMass,
                                                              emitter.SPH.RestDensity, clampedDt));
                        m_SPHShaders.PCISPHDensity->SetInt("u_GridSize", gridSize);
                        m_SPHShaders.PCISPHDensity->SetFloat("u_CellSize", cellSize);
                        m_SPHShaders.PCISPHDensity->SetFloat("u_Poly6Coeff", kp.poly6Coeff);

                        m_SPHShaders.PCISPHForce->Bind();
                        m_SPHShaders.PCISPHForce->SetInt("u_AliveCount", static_cast<int>(m_LastAliveCount));
                        m_SPHShaders.PCISPHForce->SetFloat("u_SmoothingRadius", emitter.SPH.SmoothingRadius);
                        m_SPHShaders.PCISPHForce->SetFloat("u_ParticleMass", emitter.SPH.ParticleMass);
                        m_SPHShaders.PCISPHForce->SetFloat("u_DeltaTime", clampedDt);
                        m_SPHShaders.PCISPHForce->SetInt("u_GridSize", gridSize);
                        m_SPHShaders.PCISPHForce->SetFloat("u_CellSize", cellSize);
                        m_SPHShaders.PCISPHForce->SetInt("u_RigidBodyCount", static_cast<int>(rigidBodyCount));
                        m_SPHShaders.PCISPHForce->SetFloat("u_BoundaryStiffness", emitter.SPH.BoundaryStiffness);
                        m_SPHShaders.PCISPHForce->SetFloat("u_BoundaryDamping", emitter.SPH.BoundaryDamping);
                        m_SPHShaders.PCISPHForce->SetFloat("u_SpikyCoeff", kp.spikyCoeff);
                        m_SPHShaders.PCISPHForce->SetFloat("u_WarmupTime", SPH_WARMUP_TIME);

                        // 单帧内完成所有 PCISPH 迭代（与 FluidSystemGPU 一致）
                        // 所有迭代复用初始 grid（性能优化：省去迭代 1+ 的网格重建开销）
                        for (int iter = 0; iter < iterations; iter++)
                        {
                            m_SPHShaders.PCISPHPredict->Bind();
                            RenderCommand::DispatchCompute(sphGroups);
                            RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                            m_SPHShaders.PCISPHDensity->Bind();
                            RenderCommand::DispatchCompute(sphGroups);
                            RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                            m_SPHShaders.PCISPHForce->Bind();
                            RenderCommand::DispatchCompute(sphGroups);
                            RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);
                        }

                        // 所有迭代完成，apply v* → particle.vel
                        m_SPHShaders.PCISPHApply->Bind();
                        m_SPHShaders.PCISPHApply->SetInt("u_AliveCount", static_cast<int>(m_LastAliveCount));
                        RenderCommand::DispatchCompute(sphGroups);
                        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);
                    }
                    else
                    {
                        // ---- 现有 WCSPH 路径: SPH Force ----
                        m_SPHShaders.ForceShader->Bind();
                        m_SPHShaders.ForceShader->SetInt("u_AliveCount", static_cast<int>(m_LastAliveCount));
                        m_SPHShaders.ForceShader->SetFloat("u_SmoothingRadius", emitter.SPH.SmoothingRadius);
                        m_SPHShaders.ForceShader->SetFloat("u_ParticleMass", emitter.SPH.ParticleMass);
                        m_SPHShaders.ForceShader->SetFloat("u_Viscosity", emitter.SPH.Viscosity);
                        m_SPHShaders.ForceShader->SetFloat("u_DeltaTime", clampedDt);
                        m_SPHShaders.ForceShader->SetInt("u_GridSize", gridSize);
                        m_SPHShaders.ForceShader->SetFloat("u_CellSize", cellSize);
                        // 表面张力 + 刚体耦合 uniform
                        m_SPHShaders.ForceShader->SetFloat("u_SurfaceTension", emitter.SPH.SurfaceTension);
                        m_SPHShaders.ForceShader->SetFloat("u_SpikyCoeff", kp.spikyCoeff);
                        m_SPHShaders.ForceShader->SetFloat("u_WarmupTime", SPH_WARMUP_TIME);

                        uint32_t rigidBodyCount = 0;
                        if (emitter.SPH.RigidBodyCoupling && registry)
                        {
                            InitRigidBodyBuffer();
                            rigidBodyCount =
                                UploadRigidBodiesToBuffer(registry, m_RigidBodyBuffer, MAX_RIGID_BODIES,
                                                          RigidBodyUploadFilter::RequireRigidBodyComponent);
                            m_RigidBodyBuffer->Bind(3);
                        }
                        m_SPHShaders.ForceShader->SetInt("u_RigidBodyCount", static_cast<int>(rigidBodyCount));
                        m_SPHShaders.ForceShader->SetFloat("u_BoundaryStiffness", emitter.SPH.BoundaryStiffness);
                        m_SPHShaders.ForceShader->SetFloat("u_BoundaryDamping", emitter.SPH.BoundaryDamping);

                        RenderCommand::DispatchCompute(sphGroups);
                        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);
                    }
                }

                // Rebind DeadList(1), CounterBuffer(3), IndirectArgs(4) — Grid/PCISPH passes
                // temporarily used these binding slots.
                m_DeadList->Bind(1);
                m_CounterBuffer->Bind(3);
                m_IndirectArgs->Bind(4);
            }

            // ---- Pass 3: Simulate (gravity + damping + alive/dead management) ----
            // simulate 遍历全池处理 lifetime，dispatch 全部 workgroup
            if (m_LastAliveCount > 0 || emitCount > 0)
            {
                // PCISPH handles gravity internally, so pass zero gravity to simulate pass
                glm::vec3 simGravity = (sphEnabled && emitter.SPH.PCISPHEnabled) ? glm::vec3(0.0f) : emitter.Gravity;
                float     simulateDt = std::min(dt, 0.05f);
                m_SimulateShader->Bind();
                m_SimulateShader->SetFloat("u_DeltaTime", simulateDt);
                m_SimulateShader->SetFloat3("u_Gravity", simGravity);
                m_SimulateShader->SetFloat("u_Damping", emitter.Damping);
                m_SimulateShader->SetInt("u_MaxParticles", static_cast<int>(m_MaxParticles));

                uint32_t simGroups = (m_MaxParticles + 255) / 256;
                RenderCommand::DispatchCompute(simGroups);
                RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);
            }

            // ---- Pass 4: Render Args ----
            m_RenderArgsShader->Bind();
            RenderCommand::DispatchCompute(1);
            RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage | BarrierBit::Command);

        } // end if (!cudaRan) — CUDA path skips all GL compute above

        PerformanceMonitor::Get().GetParticleComputeGPUTimer().End();

        // ---- 异步回读：用于 simulate 按存活数 dispatch + SPH + direct-draw ----
        {
            if (abConfig.DisableCounterReadback)
            {
                m_Readback->Reset();
                counterReadbackCpuMs = 0.0f;
            }
            else
            {
                const auto readbackStart = std::chrono::high_resolution_clock::now();

                // Collect previous frame's result (non-blocking)
                if (m_Readback->IsPending() && m_Readback->IsReady())
                {
                    CounterData counters{};
                    m_Readback->GetData(&counters, sizeof(CounterData));

                    CounterData sanitized = counters;
                    bool        corrected = false;

                    if (sanitized.deadCount > m_MaxParticles)
                    {
                        sanitized.deadCount = m_MaxParticles;
                        corrected           = true;
                    }
                    if (sanitized.aliveCount > m_MaxParticles)
                    {
                        sanitized.aliveCount = m_MaxParticles;
                        corrected            = true;
                    }

                    if (corrected)
                    {
                        ENGINE_WARN("[Particle] Counter overflow detected (dead={0}, alive={1}, max={2}); clamping to "
                                    "safe range.",
                                    counters.deadCount, counters.aliveCount, m_MaxParticles);
                        m_CounterBuffer->SetData(&sanitized, sizeof(CounterData), 0);
                    }

                    m_LastAliveCount = sanitized.aliveCount;

                    if (!m_UseIndirectDraw)
                        m_AliveCountForDirectDraw = sanitized.aliveCount;
                }

                // Initiate this frame's async copy
                m_Readback->CopyFrom(m_CounterBuffer, sizeof(CounterData));

                counterReadbackCpuMs =
                    std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - readbackStart)
                        .count();
            }
        }
    }

    void ParticleSystemGPU::Render(const glm::mat4& viewMatrix, const glm::mat4& projection)
    {
        if (!m_Initialized)
            return;

        // Bind buffers for vertex shader access
        m_ParticleBuffer->Bind(0);
        m_AliveList->Bind(2);

        m_BillboardShader->Bind();
        m_BillboardShader->SetMat4("u_View", viewMatrix);
        m_BillboardShader->SetMat4("u_Projection", projection);

        // Disable depth write, keep depth test
        RenderCommand::SetDepthMask(false);

        m_EmptyVAO->Bind();
        if (m_UseIndirectDraw)
            RenderCommand::DrawArraysIndirect(m_IndirectArgs->GetRendererID());
        else
            RenderCommand::DrawArraysInstanced(6, m_AliveCountForDirectDraw);

        // Restore depth write
        RenderCommand::SetDepthMask(true);
    }

#ifdef ENGINE_ENABLE_VULKAN
    // ============================================================
    // Vulkan compute 资源懒初始化
    // ============================================================
    bool ParticleSystemGPU::InitVulkanComputeResources()
    {
        if (m_VulkanResources && m_VulkanResources->Initialized)
            return true;

        if (!m_VulkanResources)
            m_VulkanResources = CreateScope<VulkanResources>();

        auto* ctx = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(ctx != nullptr, "[Particle][Vulkan] VulkanContext is null");
        VkDevice device           = ctx->GetDevice();
        m_VulkanResources->Device = device;

        auto buildPipeline = [&](const Ref<Shader>& shader, VulkanComputePipelineHandle& outHandle,
                                 Ref<VulkanDescriptorSetLayout>& outLayout) -> bool
        {
            auto vkShader = std::dynamic_pointer_cast<VulkanShader>(shader);
            ENGINE_CORE_RELEASE_ASSERT(vkShader, "[Particle][Vulkan] expected VulkanShader instance on Vulkan backend");

            VkShaderModule module = vkShader->GetOrCreateShaderModule(device, "compute");
            ENGINE_CORE_RELEASE_ASSERT(module != VK_NULL_HANDLE,
                                       "[Particle][Vulkan] compute shader module creation failed");

            outLayout = VulkanDescriptorSetLayout::CreateFromReflection(device, vkShader->GetReflectedBindings(), 0);

            std::vector<VkPushConstantRange> pcRanges;
            for (const auto& pc : vkShader->GetReflectedPushConstants())
            {
                VkPushConstantRange r{};
                r.stageFlags = pc.Stages;
                r.offset     = pc.Offset;
                r.size       = pc.Size;
                pcRanges.push_back(r);
            }

            VulkanComputePipelineDesc desc{};
            desc.ShaderModule  = module;
            desc.SetLayouts    = {outLayout->GetHandle()};
            desc.PushConstants = pcRanges;
            outHandle          = VulkanPipeline::CreateCompute(device, desc);
            return true;
        };

        buildPipeline(m_EmitShader, m_VulkanResources->EmitPipeline, m_VulkanResources->EmitLayout);
        buildPipeline(m_CompactShader, m_VulkanResources->CompactPipeline, m_VulkanResources->CompactLayout);
        buildPipeline(m_SimulateShader, m_VulkanResources->SimulatePipeline, m_VulkanResources->SimulateLayout);
        buildPipeline(m_RenderArgsShader, m_VulkanResources->RenderArgsPipeline, m_VulkanResources->RenderArgsLayout);

        // 每帧需要：emit(1) + compact(1) + simulate(1) + render_args(1) = 4 set；
        // SPH 路径再加 density(1) + force(1) + Grid 内部 N set，整体扩容到 256 给充分余量。
        m_VulkanResources->Pool = VulkanDescriptorPool::CreateDefaultComputePool(device, 256);

        m_VulkanResources->Initialized = true;
        return true;
    }

    bool ParticleSystemGPU::InitSPHVulkanPipelines()
    {
        if (m_VulkanResources->SPHInitialized)
            return true;

        VkDevice device = m_VulkanResources->Device;

        auto buildOne = [&](const Ref<Shader>& shader, Ref<VulkanDescriptorSetLayout>& outLayout,
                            VulkanComputePipelineHandle& outPipe) -> bool
        {
            auto vkShader = std::dynamic_pointer_cast<VulkanShader>(shader);
            ENGINE_CORE_RELEASE_ASSERT(vkShader, "[Particle][Vulkan] SPH shader 转型失败");

            VkShaderModule module = vkShader->GetOrCreateShaderModule(device, "compute");
            ENGINE_CORE_RELEASE_ASSERT(module != VK_NULL_HANDLE, "[Particle][Vulkan] SPH module 创建失败");

            outLayout = VulkanDescriptorSetLayout::CreateFromReflection(device, vkShader->GetReflectedBindings(), 0);

            VulkanComputePipelineDesc desc{};
            desc.ShaderModule = module;
            desc.SetLayouts   = {outLayout->GetHandle()};
            for (const auto& pc : vkShader->GetReflectedPushConstants())
            {
                VkPushConstantRange r{};
                r.stageFlags = pc.Stages;
                r.offset     = pc.Offset;
                r.size       = pc.Size;
                desc.PushConstants.push_back(r);
            }
            outPipe = VulkanPipeline::CreateCompute(device, desc);
            return true;
        };

        // 粒子 SPH 只走 WCSPH（density + force），不用 PCISPH（PCISPH 是流体专用）
        buildOne(m_SPHShaders.DensityShader, m_VulkanResources->SPHDensityLayout,
                 m_VulkanResources->SPHDensityPipeline);
        buildOne(m_SPHShaders.ForceShader, m_VulkanResources->SPHForceLayout, m_VulkanResources->SPHForcePipeline);

        m_VulkanResources->SPHInitialized = true;
        ENGINE_CORE_INFO("[Particle][Vulkan] SPH 2 compute pipeline 初始化完成 (density + force)");
        return true;
    }

    void ParticleSystemGPU::DestroyVulkanComputeResources()
    {
        if (!m_VulkanResources || !m_VulkanResources->Initialized)
            return;

        VkDevice device = m_VulkanResources->Device;
        if (device != VK_NULL_HANDLE)
            vkDeviceWaitIdle(device);

        VulkanPipeline::DestroyCompute(device, m_VulkanResources->EmitPipeline);
        VulkanPipeline::DestroyCompute(device, m_VulkanResources->CompactPipeline);
        VulkanPipeline::DestroyCompute(device, m_VulkanResources->SimulatePipeline);
        VulkanPipeline::DestroyCompute(device, m_VulkanResources->RenderArgsPipeline);

        if (m_VulkanResources->SPHInitialized)
        {
            VulkanPipeline::DestroyCompute(device, m_VulkanResources->SPHDensityPipeline);
            VulkanPipeline::DestroyCompute(device, m_VulkanResources->SPHForcePipeline);
            m_VulkanResources->SPHDensityLayout.reset();
            m_VulkanResources->SPHForceLayout.reset();
            m_VulkanResources->SPHInitialized = false;
        }

        m_VulkanResources->EmitLayout.reset();
        m_VulkanResources->CompactLayout.reset();
        m_VulkanResources->SimulateLayout.reset();
        m_VulkanResources->RenderArgsLayout.reset();
        m_VulkanResources->Pool.reset();
        m_VulkanResources->Device      = VK_NULL_HANDLE;
        m_VulkanResources->Initialized = false;
    }

    // ============================================================
    // Vulkan 路径 Update — emit / compact / SPH(density+force) / simulate / render_args
    // 与 OpenGL Update 流程对齐：
    //   1) emit（按 emitCount）
    //   2) sphEnabled ? compact 重建 alive list（SPH 需要） : skip
    //   3) sphEnabled ? Grid.BuildVulkan + SPH density + SPH force (WCSPH path)
    //   4) simulate（gravity + damping + lifecycle）
    //   5) render_args（生成 IndirectDraw 命令）
    //   6) AsyncReadback counter（异步获取上帧 aliveCount）
    //
    // 粒子 SPH 路径仅 WCSPH（无 PCISPH，因 PCISPH 是流体专用）。
    // 见 SPEC §3 D-3/D-9/D-12/D-13，§4 P-9/P-14/P-15。
    // ============================================================
    void ParticleSystemGPU::UpdateVulkan(float                           dt,
                                         const glm::vec3&                emitterPos,
                                         const ParticleEmitterComponent& emitter,
                                         entt::registry*                 registry)
    {
        auto*           ctx = VulkanContext::Get();
        VkCommandBuffer cmd = ctx ? ctx->GetCurrentFrameCommandBuffer() : VK_NULL_HANDLE;
        if (cmd == VK_NULL_HANDLE)
        {
            // BeginFrame 未调或 swapchain recreate — 当前 SmokeLayer 不会进，但 guard 必须写
            return;
        }

        const ABConfigSnapshot abConfig = GetABConfigSnapshot();

        const bool sphEnabled = emitter.SPH.Enabled && !m_DisableSPHOnDriver;
        if (emitter.SPH.Enabled && m_DisableSPHOnDriver && !m_SPHDisableLogged)
        {
            ENGINE_WARN("[Particle][Vulkan] SPH component detected but disabled by AB-config; skipping SPH dispatch.");
            m_SPHDisableLogged = true;
        }

        // 与 OpenGL 路径一致的 CPU 端预处理：reset aliveCount / 计算 emitCount / clamp
        uint32_t zero = 0;
        m_CounterBuffer->SetData(&zero, sizeof(uint32_t), 4); // aliveCount = 0

        float clampedDt = std::min(dt, 0.05f);
        m_EmitAccumulator += emitter.EmitRate * clampedDt;
        uint32_t emitCount = static_cast<uint32_t>(m_EmitAccumulator);
        m_EmitAccumulator -= static_cast<float>(emitCount);

        int totalBurst = emitter.PendingBurst + emitter.CollisionBurstCount;
        if (totalBurst > 0)
            emitCount += static_cast<uint32_t>(totalBurst);

        emitCount = std::min(emitCount, m_MaxParticles);

        m_CounterBuffer->SetData(&emitCount, sizeof(uint32_t), 8); // emitCount

        m_TotalTime += dt;

        // 懒初始化 pipeline / pool
        if (!InitVulkanComputeResources())
            return;

        // SPH 路径懒初始化 SPH pipeline + grid + UBO
        bool sphReady = false;
        if (sphEnabled)
        {
            if (!m_SPHInitialized)
                InitSPH(emitter.SPH.SmoothingRadius);
            sphReady = InitSPHVulkanPipelines();
            if (!sphReady)
            {
                ENGINE_CORE_WARN("[Particle][Vulkan] SPH pipeline 初始化失败，跳过 SPH dispatch");
            }
        }

        VulkanCommandBuffer cmdBuf(cmd);
        VkDevice            device = m_VulkanResources->Device;

        // P-15：每帧首次必须 Reset() 子系统自己的 pool（与 Grid 的 ResetFrameResources 各管各的）
        m_VulkanResources->Pool->Reset();

        // 向下转型 SSBO → VulkanStorageBuffer 拿原生 VkBuffer 句柄
        auto bufferOf = [](const Ref<ShaderStorageBuffer>& ssbo) -> VkBuffer
        {
            auto v = std::dynamic_pointer_cast<VulkanStorageBuffer>(ssbo);
            ENGINE_CORE_RELEASE_ASSERT(v, "[Particle][Vulkan] SSBO is not a VulkanStorageBuffer");
            return v->GetBuffer();
        };

        VkBuffer particleBuf    = bufferOf(m_ParticleBuffer);
        VkBuffer deadListBuf    = bufferOf(m_DeadList);
        VkBuffer aliveListBuf   = bufferOf(m_AliveList);
        VkBuffer counterBuf     = bufferOf(m_CounterBuffer);
        VkBuffer indirectArgBuf = bufferOf(m_IndirectArgs);

        auto vkEmitUBO = std::dynamic_pointer_cast<VulkanUniformBuffer>(m_EmitParamsUBO);
        auto vkSimUBO  = std::dynamic_pointer_cast<VulkanUniformBuffer>(m_SimParamsUBO);
        ENGINE_CORE_RELEASE_ASSERT(vkEmitUBO && vkSimUBO, "[Particle][Vulkan] UBO 转型失败");

        const VulkanBarrierMasks ssboBarrier    = ResolveBarrierBits(BarrierBit::ShaderStorage);
        const VulkanBarrierMasks ssboCmdBarrier = ResolveBarrierBits(BarrierBit::ShaderStorage | BarrierBit::Command);

        auto ssboBarrierFn = [&]()
        {
            cmdBuf.MemoryBarrier(ssboBarrier.SrcStage, ssboBarrier.DstStage, ssboBarrier.SrcAccess,
                                 ssboBarrier.DstAccess);
        };

        PerformanceMonitor::Get().GetParticleComputeGPUTimer().Begin();

        // ============================================================
        // Pass 1: Emit
        // ============================================================
        if (emitCount > 0)
        {
            ParticleEmitParamsUBO ubo{};
            float                 safeLifeMin = std::max(emitter.LifeMin, 1e-6f);
            float                 safeLifeMax = std::max(emitter.LifeMax, safeLifeMin);
            ubo.EmitterPosAndAngle            = glm::vec4(emitterPos, glm::radians(emitter.EmitAngle));
            ubo.EmitDirAndSpeedMin            = glm::vec4(emitter.EmitDirection, emitter.SpeedMin);
            ubo.LifeAndSpeedMax               = glm::vec4(safeLifeMin, safeLifeMax, emitter.SpeedMax, 0.0f);
            ubo.SizeStartEnd                  = glm::vec4(emitter.SizeStart, emitter.SizeEnd, 0.0f, 0.0f);
            ubo.StartColor                    = emitter.ColorStart;
            ubo.EndColor                      = emitter.ColorEnd;
            m_EmitParamsUBO->SetData(&ubo, sizeof(ubo));

            VkDescriptorSet set = m_VulkanResources->Pool->Allocate(m_VulkanResources->EmitLayout->GetHandle());
            ENGINE_CORE_RELEASE_ASSERT(set != VK_NULL_HANDLE, "[Particle][Vulkan] emit descriptor alloc failed");

            VulkanDescriptorWriter w;
            w.WriteBuffer(0, particleBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(1, deadListBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(3, counterBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(PARTICLE_EMIT_UBO_BINDING, vkEmitUBO->GetBuffer(), 0, sizeof(ParticleEmitParamsUBO),
                          VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
            w.UpdateSet(device, set);

            cmdBuf.BindComputePipeline(m_VulkanResources->EmitPipeline.Pipeline);
            cmdBuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, m_VulkanResources->EmitPipeline.Layout, 0, {set});

            ParticleEmitPC pc{emitCount, m_MaxParticles, m_TotalTime, 0u};
            cmdBuf.PushConstants(m_VulkanResources->EmitPipeline.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc),
                                 &pc);

            uint32_t groups = (emitCount + 63) / 64;
            if (groups > 0)
                cmdBuf.Dispatch(groups, 1, 1);

            ssboBarrierFn();
        }

        // ============================================================
        // Pass 1.5: Compact (SPH 需要新生粒子的精确 alive list；非 SPH 路径跳过)
        // ============================================================
        if (sphReady)
        {
            // 重置 counter 让 compact 重新 build alive/dead
            CounterData cz{};
            m_CounterBuffer->SetData(&cz, sizeof(CounterData), 0);
            ssboBarrierFn();

            VkDescriptorSet set = m_VulkanResources->Pool->Allocate(m_VulkanResources->CompactLayout->GetHandle());
            ENGINE_CORE_RELEASE_ASSERT(set != VK_NULL_HANDLE, "[Particle][Vulkan] compact descriptor alloc failed");

            VulkanDescriptorWriter w;
            w.WriteBuffer(0, particleBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(1, deadListBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(2, aliveListBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(3, counterBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.UpdateSet(device, set);

            cmdBuf.BindComputePipeline(m_VulkanResources->CompactPipeline.Pipeline);
            cmdBuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, m_VulkanResources->CompactPipeline.Layout, 0,
                                      {set});

            ParticleCompactPC compactPC{m_MaxParticles};
            cmdBuf.PushConstants(m_VulkanResources->CompactPipeline.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                 sizeof(compactPC), &compactPC);

            uint32_t compactGroups = (m_MaxParticles + 255) / 256;
            if (compactGroups > 0)
                cmdBuf.Dispatch(compactGroups, 1, 1);

            ssboBarrierFn();
        }

        // ============================================================
        // Pass 2: SPH (density + force) — WCSPH only
        // 使用上一帧异步回读的 m_LastAliveCount（1 帧延迟可接受）
        // ============================================================
        if (sphReady && m_LastAliveCount > 0)
        {
            SPHKernelParams kp = SPHKernelParams::Compute(emitter.SPH.SmoothingRadius);

            // 收集刚体（rigid body coupling），粒子 SPH 不用 MeshSDF（与 OpenGL 路径一致）
            uint32_t rigidBodyCount = 0;
            if (emitter.SPH.RigidBodyCoupling && registry)
            {
                InitRigidBodyBuffer();
                rigidBodyCount = UploadRigidBodiesToBuffer(registry, m_RigidBodyBuffer, MAX_RIGID_BODIES,
                                                           RigidBodyUploadFilter::RequireRigidBodyComponent);
            }

            // 上传 SPH 共享 UBO
            ParticleSPHParamsUBO sphUbo{};
            sphUbo.GravityAndSmoothingRadius = glm::vec4(emitter.Gravity, emitter.SPH.SmoothingRadius);
            sphUbo.MassDensityGasViscosity   = glm::vec4(emitter.SPH.ParticleMass, emitter.SPH.RestDensity,
                                                         emitter.SPH.GasConstant, emitter.SPH.Viscosity);
            sphUbo.GridParams =
                glm::vec4(static_cast<float>(m_Grid.GetGridSize()), m_Grid.GetCellSize(), kp.poly6Coeff, kp.spikyCoeff);
            sphUbo.BoundaryParams = glm::vec4(emitter.SPH.BoundaryStiffness, emitter.SPH.BoundaryDamping,
                                              SPH_WARMUP_TIME, emitter.SPH.SurfaceTension);
            sphUbo.SDFCounts      = glm::vec4(static_cast<float>(rigidBodyCount), 0.0f, 0.0f, 0.0f);
            m_SPHParamsUBO->SetData(&sphUbo, sizeof(sphUbo));

            auto vkSPHUBO = std::dynamic_pointer_cast<VulkanUniformBuffer>(m_SPHParamsUBO);
            ENGINE_CORE_RELEASE_ASSERT(vkSPHUBO, "[Particle][Vulkan] SPH UBO 转型失败");

            // 粒子 SPH binding=8 用 alive list 占位（无专属 SurfaceNormalBuffer）。
            // shader 内 u_SurfaceTension>0 时会向该 binding 写入，会损坏 alive list。
            // 若启用粒子表面张力，需先实装 m_SurfaceNormalBuffer。
            ENGINE_CORE_RELEASE_ASSERT(
                emitter.SPH.SurfaceTension == 0.0f,
                "[Particle][Vulkan] SurfaceTension>0 需要专属 SurfaceNormalBuffer，当前 binding=8 是 alive list 占位");

            // Grid 注入外部 buffer + 每帧首次 ResetFrameResources（D-5/D-12）
            m_Grid.SetExternalBuffers(m_ParticleBuffer, m_AliveList, nullptr);
            m_Grid.ResetFrameResources();
            m_Grid.BuildVulkan(cmd, m_LastAliveCount, /*predicted=*/false);
            ssboBarrierFn();

            auto dispatchSPH = [&](const VulkanComputePipelineHandle&    pipe,
                                   const Ref<VulkanDescriptorSetLayout>& layout, bool bindRigid, bool bindSN)
            {
                VkDescriptorSet set = m_VulkanResources->Pool->Allocate(layout->GetHandle());
                ENGINE_CORE_RELEASE_ASSERT(set != VK_NULL_HANDLE, "[Particle][Vulkan] SPH pool 耗尽");

                VulkanDescriptorWriter w;
                w.WriteBuffer(0, particleBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                w.WriteBuffer(2, aliveListBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                if (bindRigid && m_RigidBodyBuffer)
                    w.WriteBuffer(3, bufferOf(m_RigidBodyBuffer), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                w.WriteBuffer(5, bufferOf(m_Grid.GetCellStart()), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                w.WriteBuffer(6, bufferOf(m_Grid.GetCellCount()), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                w.WriteBuffer(7, bufferOf(m_Grid.GetSortedIndices()), 0, VK_WHOLE_SIZE,
                              VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                // 粒子 SPH 没有专属 surface normal buffer（OpenGL 路径也没分配），
                // density shader 仍声明 binding=8 — 用 alive list 占位以满足布局，
                // shader 内仅在 u_SurfaceTension > 0 时写入；粒子默认 SurfaceTension=0 时未触发，
                // 写入位置实际是 alive list 但不会被采样。这是当前 OpenGL 路径同款 trade-off。
                // TODO: 若启用粒子 surface tension，需补 m_SurfaceNormalBuffer。
                if (bindSN)
                    w.WriteBuffer(8, aliveListBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                // MeshSDF binding（粒子 SPH 不用，按未绑定语义跳过）

                w.WriteBuffer(PARTICLE_SPH_UBO_BINDING, vkSPHUBO->GetBuffer(), 0, sizeof(ParticleSPHParamsUBO),
                              VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
                w.UpdateSet(device, set);

                cmdBuf.BindComputePipeline(pipe.Pipeline);
                cmdBuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, pipe.Layout, 0, {set});

                ParticleSPHPushConstants pcVal{m_LastAliveCount, clampedDt, 0u};
                cmdBuf.PushConstants(pipe.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcVal), &pcVal);

                uint32_t groups = (m_LastAliveCount + 255) / 256;
                if (groups > 0)
                    cmdBuf.Dispatch(groups, 1, 1);
                ssboBarrierFn();
            };

            // SPH Density
            dispatchSPH(m_VulkanResources->SPHDensityPipeline, m_VulkanResources->SPHDensityLayout,
                        /*Rigid=*/false, /*SN=*/true);
            // SPH Force（WCSPH）
            dispatchSPH(m_VulkanResources->SPHForcePipeline, m_VulkanResources->SPHForceLayout,
                        /*Rigid=*/true, /*SN=*/true);
        }

        // ============================================================
        // Pass 3: Simulate (gravity + damping + alive/dead management)
        // ============================================================
        if (m_LastAliveCount > 0 || emitCount > 0)
        {
            // SPH 路径 simulate 重力保持非零（粒子 SPH 不用 PCISPH-style 零重力 — 粒子 SPH force shader 不叠重力）
            glm::vec3 simGravity = emitter.Gravity;
            float     simulateDt = std::min(dt, 0.05f);

            ParticleSimParamsUBO simUbo{};
            simUbo.GravityAndDamping = glm::vec4(simGravity, emitter.Damping);
            m_SimParamsUBO->SetData(&simUbo, sizeof(simUbo));

            VkDescriptorSet set = m_VulkanResources->Pool->Allocate(m_VulkanResources->SimulateLayout->GetHandle());
            ENGINE_CORE_RELEASE_ASSERT(set != VK_NULL_HANDLE, "[Particle][Vulkan] simulate descriptor alloc failed");

            VulkanDescriptorWriter w;
            w.WriteBuffer(0, particleBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(1, deadListBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(2, aliveListBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(3, counterBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(PARTICLE_SIM_UBO_BINDING, vkSimUBO->GetBuffer(), 0, sizeof(ParticleSimParamsUBO),
                          VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
            w.UpdateSet(device, set);

            cmdBuf.BindComputePipeline(m_VulkanResources->SimulatePipeline.Pipeline);
            cmdBuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, m_VulkanResources->SimulatePipeline.Layout, 0,
                                      {set});

            ParticleSimulatePC pc{simulateDt, m_MaxParticles, 0u};
            cmdBuf.PushConstants(m_VulkanResources->SimulatePipeline.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc),
                                 &pc);

            uint32_t simGroups = (m_MaxParticles + 255) / 256;
            if (simGroups > 0)
                cmdBuf.Dispatch(simGroups, 1, 1);

            cmdBuf.MemoryBarrier(ssboBarrier.SrcStage, ssboBarrier.DstStage, ssboBarrier.SrcAccess,
                                 ssboBarrier.DstAccess);
        }

        // ============================================================
        // Pass 4: Render Args
        // ============================================================
        {
            VkDescriptorSet set = m_VulkanResources->Pool->Allocate(m_VulkanResources->RenderArgsLayout->GetHandle());
            ENGINE_CORE_RELEASE_ASSERT(set != VK_NULL_HANDLE, "[Particle][Vulkan] render_args descriptor alloc failed");

            VulkanDescriptorWriter w;
            w.WriteBuffer(3, counterBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(4, indirectArgBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.UpdateSet(device, set);

            cmdBuf.BindComputePipeline(m_VulkanResources->RenderArgsPipeline.Pipeline);
            cmdBuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, m_VulkanResources->RenderArgsPipeline.Layout, 0,
                                      {set});

            ParticleRenderArgsPC pc{m_MaxParticles};
            cmdBuf.PushConstants(m_VulkanResources->RenderArgsPipeline.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                 sizeof(pc), &pc);

            cmdBuf.Dispatch(1, 1, 1);

            // ShaderStorage + Command bit → IndirectArgs 写完后供 DrawArraysIndirect 读取
            cmdBuf.MemoryBarrier(ssboCmdBarrier.SrcStage, ssboCmdBarrier.DstStage, ssboCmdBarrier.SrcAccess,
                                 ssboCmdBarrier.DstAccess);
        }

        PerformanceMonitor::Get().GetParticleComputeGPUTimer().End();

        // ============================================================
        // AsyncReadback —— Vulkan 路径下工厂返回 VulkanAsyncReadback，接口零修改
        // ============================================================
        if (abConfig.DisableCounterReadback)
        {
            m_Readback->Reset();
        }
        else
        {
            // Collect previous frame's result (non-blocking)
            if (m_Readback->IsPending() && m_Readback->IsReady())
            {
                CounterData counters{};
                m_Readback->GetData(&counters, sizeof(CounterData));

                CounterData sanitized = counters;
                bool        corrected = false;

                if (sanitized.deadCount > m_MaxParticles)
                {
                    sanitized.deadCount = m_MaxParticles;
                    corrected           = true;
                }
                if (sanitized.aliveCount > m_MaxParticles)
                {
                    sanitized.aliveCount = m_MaxParticles;
                    corrected            = true;
                }

                if (corrected)
                {
                    ENGINE_WARN("[Particle] Counter overflow detected (dead={0}, alive={1}, max={2}); "
                                "clamping to safe range.",
                                counters.deadCount, counters.aliveCount, m_MaxParticles);
                    m_CounterBuffer->SetData(&sanitized, sizeof(CounterData), 0);
                }

                m_LastAliveCount = sanitized.aliveCount;

                if (!m_UseIndirectDraw)
                    m_AliveCountForDirectDraw = sanitized.aliveCount;
            }

            // 录入主帧 cmd 的 vkCmdCopyBuffer（VulkanAsyncReadback 内部）
            m_Readback->CopyFrom(m_CounterBuffer, sizeof(CounterData));
        }
    }
#endif // ENGINE_ENABLE_VULKAN

} // namespace Engine
