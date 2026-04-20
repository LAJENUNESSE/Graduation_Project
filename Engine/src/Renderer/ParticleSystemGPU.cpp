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

namespace Engine
{

    // =========================================================================
    // CudaImpl stub (CUDA removed)
    // =========================================================================
    struct ParticleSystemGPU::CudaImpl
    {
    }; // Empty stub

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

            LogParticleABSummaryIfChanged("Init");
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

    ParticleSystemGPU::ParticleSystemGPU(uint32_t maxParticles)
        : m_MaxParticles(maxParticles), m_CudaImpl(CreateScope<CudaImpl>())
    {
    }

    ParticleSystemGPU::~ParticleSystemGPU()
    {
        // CudaImpl 析构函数处理 CUDA 资源清理
        // m_Readback via Ref<GPUAsyncReadback> auto-destructs
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
        // CUDA removed - always return CudaGL as placeholder
        return InteropBackend::CudaGL;
    }

    const char* ParticleSystemGPU::InteropBackendLabel(InteropBackend backend)
    {
        switch (backend)
        {
        case InteropBackend::VulkanExternal:
            return "VulkanExternal (deprecated)";
        case InteropBackend::CudaVulkan:
            return "CudaVulkan";
        case InteropBackend::CudaGL:
        default:
            return "CudaGL";
        }
    }

    bool ParticleSystemGPU::IsVkExtSkeletonReady()
    {
        return false;
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

        m_Initialized = true;
    }

    void ParticleSystemGPU::InitSPH(float smoothingRadius)
    {
        if (m_SPHInitialized)
            return;

        // Grid cell size = 2 * smoothing radius (保证邻域在 3x3x3 cell 内)
        float    cellSize = 2.0f * smoothingRadius;
        uint32_t gridSize = 64;

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

        // GL Compute path
        PerformanceMonitor::Get().GetParticleComputeGPUTimer().Begin();

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
                        rigidBodyCount = UploadRigidBodiesToBuffer(registry, m_RigidBodyBuffer, MAX_RIGID_BODIES,
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
                    // 自适应 grid 策略：迭代 0 复用原始 grid，迭代 1+ 用预测位置重建
                    for (int iter = 0; iter < iterations; iter++)
                    {
                        // 迭代 1+：用预测位置重建空间哈希 grid
                        if (iter > 0)
                        {
                            m_PCISPHBuffer->Bind(9); // binding 9: PCISPHData for predicted pos
                            m_Grid.Build(m_LastAliveCount, true);
                            // 重新绑定 PCISPH 使用的 buffer slots
                            m_ParticleBuffer->Bind(0);
                            m_AliveList->Bind(2);
                            m_PCISPHBuffer->Bind(1);
                            if (m_RigidBodyBuffer)
                                m_RigidBodyBuffer->Bind(3);
                        }

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
                        rigidBodyCount = UploadRigidBodiesToBuffer(registry, m_RigidBodyBuffer, MAX_RIGID_BODIES,
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

} // namespace Engine
