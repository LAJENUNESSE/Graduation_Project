#include "engpch.h"
#include "Renderer/ParticleSystemGPU.h"
#include "Renderer/SPHCommon.h"
#include "Renderer/SPHKernelMath.h"
#include "Core/Log.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/RendererAPI.h"
#include "Scene/Components.h"

#include <glad/gl.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

#ifdef ENGINE_ENABLE_CUDA
#include "Platform/CUDA/CudaGLInteropContext.h"
#include "Platform/CUDA/CudaParticlePipeline.h"
#include "Platform/CUDA/CudaParticleTypes.h"
#include "Platform/CUDA/CudaSPHPipeline.h"
#include "Platform/CUDA/CudaErrorHandling.h"
#endif

#include "Debug/PerformanceMonitor.h"

namespace Engine
{

    namespace
    {
        // SPH warm-up: 新粒子在此时间内 SPH 压力力从 0 渐入到 100%
        // 防止爆发 (burst) 发射时密度冲击导致位置突变/闪烁
        constexpr float SPH_WARMUP_TIME = 0.08f; // 秒（约 5 帧 @ 60 FPS）

        bool ContainsToken(const char* str, const char* token)
        {
            return str && token && std::strstr(str, token) != nullptr;
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
    // 交叉验证 C++ 结构体与 CUDA 共享 POD 类型的内存布局一致性
    static_assert(sizeof(GPUParticleData) == sizeof(CudaInterop::GPUParticle),
                  "GPUParticleData / CudaInterop::GPUParticle size mismatch");
    static_assert(sizeof(CounterData) == sizeof(CudaInterop::CounterData), "CounterData size mismatch");
    static_assert(sizeof(IndirectDrawCommand) == sizeof(CudaInterop::IndirectDrawCommand),
                  "IndirectDrawCommand size mismatch");

    static_assert(offsetof(GPUParticleData, posAndLife) == offsetof(CudaInterop::GPUParticle, posAndLife), "");
    static_assert(offsetof(GPUParticleData, velAndMaxLife) == offsetof(CudaInterop::GPUParticle, velAndMaxLife), "");
    static_assert(offsetof(GPUParticleData, startColor) == offsetof(CudaInterop::GPUParticle, startColor), "");
    static_assert(offsetof(GPUParticleData, endColor) == offsetof(CudaInterop::GPUParticle, endColor), "");
    static_assert(offsetof(GPUParticleData, params) == offsetof(CudaInterop::GPUParticle, params), "");

    static_assert(offsetof(CounterData, deadCount) == offsetof(CudaInterop::CounterData, deadCount), "");
    static_assert(offsetof(CounterData, aliveCount) == offsetof(CudaInterop::CounterData, aliveCount), "");
    static_assert(offsetof(CounterData, emitCount) == offsetof(CudaInterop::CounterData, emitCount), "");

    // IndirectDrawCommand 字段名不同但 offset 必须匹配
    static_assert(offsetof(IndirectDrawCommand, count) == offsetof(CudaInterop::IndirectDrawCommand, vertexCount), "");
    static_assert(offsetof(IndirectDrawCommand, instanceCount) ==
                      offsetof(CudaInterop::IndirectDrawCommand, instanceCount),
                  "");
    static_assert(offsetof(IndirectDrawCommand, first) == offsetof(CudaInterop::IndirectDrawCommand, firstVertex), "");
    static_assert(offsetof(IndirectDrawCommand, baseInstance) ==
                      offsetof(CudaInterop::IndirectDrawCommand, baseInstance),
                  "");
#endif

    ParticleSystemGPU::ParticleSystemGPU(uint32_t maxParticles) : m_MaxParticles(maxParticles) {}

    ParticleSystemGPU::~ParticleSystemGPU()
    {
#ifdef ENGINE_ENABLE_CUDA
        if (m_CudaSPHCtx)
            CudaInterop::DestroySPHContext(m_CudaSPHCtx);
        m_CudaTiming.Destroy();
#endif
        if (m_ReadbackFence)
            glDeleteSync(static_cast<GLsync>(m_ReadbackFence));
        if (m_ReadbackBuffer)
            glDeleteBuffers(1, &m_ReadbackBuffer);
    }

    void ParticleSystemGPU::Init()
    {
        if (m_Initialized)
            return;

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
        glGenBuffers(1, &m_ReadbackBuffer);
        glBindBuffer(GL_COPY_WRITE_BUFFER, m_ReadbackBuffer);
        glBufferData(GL_COPY_WRITE_BUFFER, sizeof(CounterData), nullptr, GL_STREAM_READ);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);

        const char* vendor   = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));

        // VMware SVGA has known instability with advanced compute/indirect paths.
        bool vmwareDriver  = ContainsToken(vendor, "VMware") || ContainsToken(renderer, "SVGA3D");
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

#ifdef ENGINE_ENABLE_CUDA
        // ---- CUDA sidecar 初始化（一次性，失败后永久走 GL）----
        if (!m_CudaInitAttempted)
        {
            m_CudaInitAttempted = true;
            if (!CudaInterop::IsCudaPoisoned() && CudaGLInteropContext::ProbeDeviceMatch())
            {
                m_CudaInterop      = CreateScope<CudaGLInteropContext>();
                m_CudaSlotParticle = m_CudaInterop->RegisterBuffer(m_ParticleBuffer->GetRendererID(), "ParticleBuffer");
                m_CudaSlotDeadList = m_CudaInterop->RegisterBuffer(m_DeadList->GetRendererID(), "DeadList");
                m_CudaSlotAliveList = m_CudaInterop->RegisterBuffer(m_AliveList->GetRendererID(), "AliveList");
                m_CudaSlotCounter   = m_CudaInterop->RegisterBuffer(m_CounterBuffer->GetRendererID(), "CounterBuffer");
                m_CudaSlotIndirect  = m_CudaInterop->RegisterBuffer(m_IndirectArgs->GetRendererID(), "IndirectArgs");

                if (m_CudaSlotParticle >= 0 && m_CudaSlotDeadList >= 0 && m_CudaSlotAliveList >= 0 &&
                    m_CudaSlotCounter >= 0 && m_CudaSlotIndirect >= 0)
                {
                    m_UseCudaPath = true;
                    m_CudaTiming.Init();
                    m_CudaSPHCtx = CudaInterop::CreateSPHContext(m_MaxParticles, 64, MAX_RIGID_BODIES);
                    if (!m_CudaSPHCtx)
                        ENGINE_WARN("[Particle] CUDA CreateSPHContext failed; SPH will use GL compute.");
                    ENGINE_INFO("[Particle] CUDA compute sidecar activated ({0} buffers registered).",
                                m_CudaInterop->GetSlotCount());
                }
                else
                {
                    ENGINE_WARN("[Particle] CUDA buffer registration partially failed; falling back to GL compute.");
                    m_CudaInterop.reset();
                }
            }
            else
            {
                ENGINE_INFO("[Particle] No CUDA-capable device matching GL context; using GL compute path.");
            }
        }
#endif
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

#ifdef ENGINE_ENABLE_CUDA
        // ---- CUDA compute sidecar path ----
        bool cudaSucceeded = false;
        if (m_UseCudaPath && !CudaInterop::IsCudaPoisoned())
        {
            if (sphEnabled && m_CudaSPHCtx)
            {
                // ===== CUDA SPH 路径: GL Emit+Compact → CUDA SPH → GL RenderArgs =====
                PerformanceMonitor::Get().GetParticleComputeGPUTimer().Begin();

                // ---- Phase 1 (GL): Emit + Compact + Counter 回读 ----
                m_ParticleBuffer->Bind(0);
                m_DeadList->Bind(1);
                m_AliveList->Bind(2);
                m_CounterBuffer->Bind(3);
                m_IndirectArgs->Bind(4);

                if (emitCount > 0)
                {
                    m_EmitShader->Bind();
                    m_EmitShader->SetFloat3("u_EmitterPos", emitterPos);
                    m_EmitShader->SetFloat3("u_EmitDirection", emitter.EmitDirection);
                    m_EmitShader->SetFloat("u_EmitAngle", glm::radians(emitter.EmitAngle));
                    m_EmitShader->SetFloat("u_LifeMin", emitter.LifeMin);
                    m_EmitShader->SetFloat("u_LifeMax", emitter.LifeMax);
                    m_EmitShader->SetFloat("u_SpeedMin", emitter.SpeedMin);
                    m_EmitShader->SetFloat("u_SpeedMax", emitter.SpeedMax);
                    m_EmitShader->SetFloat("u_SizeStart", emitter.SizeStart);
                    m_EmitShader->SetFloat("u_SizeEnd", emitter.SizeEnd);
                    m_EmitShader->SetFloat4("u_StartColor", emitter.ColorStart);
                    m_EmitShader->SetFloat4("u_EndColor", emitter.ColorEnd);
                    m_EmitShader->SetInt("u_MaxParticles", static_cast<int>(m_MaxParticles));
                    m_TotalTime += dt;
                    m_EmitShader->SetFloat("u_Time", m_TotalTime);

                    uint32_t groups = (emitCount + 63) / 64;
                    RenderCommand::DispatchCompute(groups);
                    RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);
                }

                // Compact: 重建 alive/dead lists
                {
                    CounterData zeroC{};
                    m_CounterBuffer->SetData(&zeroC, sizeof(CounterData), 0);
                    RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                    m_CompactShader->Bind();
                    m_CompactShader->SetInt("u_MaxParticles", static_cast<int>(m_MaxParticles));
                    uint32_t compactGroups = (m_MaxParticles + 255) / 256;
                    RenderCommand::DispatchCompute(compactGroups);
                    RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                    // 不做同步回读（glGetBufferSubData 会导致 ~28ms GPU pipeline stall）。
                    // 使用上一帧异步回读的 m_LastAliveCount，1 帧延迟对 SPH dispatch 可接受。
                }

                // ---- Phase 2 (CUDA): SPH 核心计算 ----
                if (m_LastAliveCount > 0)
                {
                    // glFlush 确保 GL 命令提交到驱动队列，
                    // cudaGraphicsMapResources 负责跨 API 同步。
                    glFlush();

                    if (m_CudaInterop->MapAll())
                    {
                        void* stream       = m_CudaInterop->GetStream();
                        void* devParticles = m_CudaInterop->GetMappedPointer(m_CudaSlotParticle);
                        CudaInterop::RecordCudaEvent(m_CudaTiming.EventStart, stream);

                        SPHKernelParams kp       = SPHKernelParams::Compute(emitter.SPH.SmoothingRadius);
                        float           cellSize = 2.0f * kp.h;

                        // Grid Build
                        CudaInterop::LaunchSPHGridBuild(m_CudaSPHCtx, devParticles, m_LastAliveCount, 64, cellSize,
                                                        stream);

                        // SPH 参数
                        CudaInterop::SPHParams p{};
                        p.smoothingRadius = kp.h;
                        p.poly6Coeff      = kp.poly6Coeff;
                        p.spikyCoeff      = kp.spikyCoeff;
                        p.particleMass    = emitter.SPH.ParticleMass;
                        p.restDensity     = emitter.SPH.RestDensity;
                        p.gasConstant     = emitter.SPH.GasConstant;
                        p.viscosity       = emitter.SPH.Viscosity;
                        p.surfaceTension  = emitter.SPH.SurfaceTension;
                        p.deltaTime       = clampedDt;
                        p.gridSize        = 64;
                        p.cellSize        = cellSize;
                        p.aliveCount      = static_cast<int>(m_LastAliveCount);
                        p.gravity[0]      = emitter.Gravity.x;
                        p.gravity[1]      = emitter.Gravity.y;
                        p.gravity[2]      = emitter.Gravity.z;
                        p.warmupTime      = SPH_WARMUP_TIME;

                        // 刚体数据：CPU→CUDA 上传
                        uint32_t cudaRigidBodyCount = 0;
                        if (emitter.SPH.RigidBodyCoupling && registry)
                        {
                            auto bodies        = CollectRigidBodies(registry, MAX_RIGID_BODIES,
                                                                    RigidBodyUploadFilter::RequireRigidBodyComponent);
                            cudaRigidBodyCount = static_cast<uint32_t>(bodies.size());
                            if (!bodies.empty())
                                CudaInterop::SPHUploadRigidBodies(m_CudaSPHCtx, bodies.data(), cudaRigidBodyCount);
                        }

                        // Density
                        CudaInterop::LaunchSPHDensity(m_CudaSPHCtx, devParticles, p, stream);

                        CudaInterop::PCISPHIterParams ip{};
                        ip.pcisphDelta = SPHKernelMath::ComputePCISPHDelta(
                            emitter.SPH.SmoothingRadius, emitter.SPH.ParticleMass, emitter.SPH.RestDensity, clampedDt);
                        ip.boundaryStiffness = emitter.SPH.BoundaryStiffness;
                        ip.boundaryDamping   = emitter.SPH.BoundaryDamping;
                        ip.rigidBodyCount    = static_cast<int>(cudaRigidBodyCount);
                        ip.usePredictedPos   = 0;

                        if (emitter.SPH.PCISPHEnabled)
                        {
                            // PCISPH 路径
                            CudaInterop::LaunchPCISPHInit(m_CudaSPHCtx, devParticles, p, stream);
                            int iterations = std::clamp(emitter.SPH.PCISPHIterations, 1, 8);
                            for (int iter = 0; iter < iterations; ++iter)
                            {
                                if (iter > 0)
                                    CudaInterop::LaunchSPHGridBuild(m_CudaSPHCtx, devParticles, m_LastAliveCount, 64,
                                                                    cellSize, stream, true);
                                CudaInterop::LaunchPCISPHPredict(m_CudaSPHCtx, devParticles, clampedDt,
                                                                 static_cast<int>(m_LastAliveCount), stream);
                                ip.usePredictedPos = (iter > 0) ? 1 : 0;
                                CudaInterop::LaunchPCISPHDensity(m_CudaSPHCtx, devParticles, p, ip, stream);
                                CudaInterop::LaunchPCISPHForce(m_CudaSPHCtx, devParticles, p, ip, stream);
                            }
                            CudaInterop::LaunchPCISPHApply(m_CudaSPHCtx, devParticles,
                                                           static_cast<int>(m_LastAliveCount), stream);
                        }
                        else
                        {
                            // WCSPH 路径
                            CudaInterop::LaunchSPHForce(m_CudaSPHCtx, devParticles, p, ip, stream);
                        }

                        // SPH Simulate: gravity + damping + position integration
                        CudaInterop::SPHSimulateParams sp{};
                        sp.deltaTime     = clampedDt;
                        sp.damping       = emitter.Damping;
                        sp.gravity[0]    = emitter.SPH.PCISPHEnabled ? 0.0f : emitter.Gravity.x;
                        sp.gravity[1]    = emitter.SPH.PCISPHEnabled ? 0.0f : emitter.Gravity.y;
                        sp.gravity[2]    = emitter.SPH.PCISPHEnabled ? 0.0f : emitter.Gravity.z;
                        sp.useBoundary   = 0;
                        sp.particleCount = static_cast<int>(m_LastAliveCount);
                        CudaInterop::LaunchSPHSimulate(devParticles, sp, stream);

                        // Life 管理：递减 life + 重建 alive/dead lists
                        // SPH 的 FluidSimulateKernel 不处理 life，需要单独处理
                        {
                            CudaInterop::LaunchLifeUpdate(
                                devParticles, m_CudaInterop->GetMappedPointer(m_CudaSlotDeadList),
                                m_CudaInterop->GetMappedPointer(m_CudaSlotAliveList),
                                m_CudaInterop->GetMappedPointer(m_CudaSlotCounter), clampedDt, m_MaxParticles, stream);
                            // RenderArgs 也在 CUDA 侧完成
                            CudaInterop::LaunchRenderArgs(m_CudaInterop->GetMappedPointer(m_CudaSlotCounter),
                                                          m_CudaInterop->GetMappedPointer(m_CudaSlotIndirect), stream);
                        }

                        CudaInterop::RecordCudaEvent(m_CudaTiming.EventStop, stream);
                        m_CudaInterop->UnmapAll();

                        if (CudaInterop::IsCudaPoisoned())
                        {
                            ENGINE_WARN("[Particle] CUDA poisoned during SPH compute ({}); falling back to GL.",
                                        CudaInterop::GetCudaPoisonReason());
                            m_UseCudaPath = false;
                        }
                        else
                        {
                            if (m_CudaTiming.HasPrevTiming && m_CudaTiming.PrevStop)
                            {
                                float ms =
                                    CudaInterop::CudaEventElapsedMs(m_CudaTiming.PrevStart, m_CudaTiming.PrevStop);
                                if (ms >= 0.0f)
                                    PerformanceMonitor::Get().SetParticleComputeCudaMs(ms);
                            }
                            m_CudaTiming.SwapEvents();
                        }
                    }
                    else
                    {
                        ENGINE_WARN("[Particle] CUDA MapAll failed ({}); permanently falling back to GL compute.",
                                    CudaInterop::GetCudaPoisonReason());
                        m_UseCudaPath = false;
                    }
                }

                // ---- Phase 3: 计时结束 ----
                // RenderArgs 已在 CUDA 侧的 LifeUpdate 后完成
                PerformanceMonitor::Get().GetParticleComputeGPUTimer().End();
                cudaSucceeded = true;
            }
            else if (!sphEnabled)
            {
                if (m_CudaInterop->MapAll())
                {
                    void* stream = m_CudaInterop->GetStream();
                    CudaInterop::RecordCudaEvent(m_CudaTiming.EventStart, stream);

                    // Emit
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
                        ep.lifeMin          = emitter.LifeMin;
                        ep.lifeMax          = emitter.LifeMax;
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
                        m_TotalTime += dt;
                        ep.time         = m_TotalTime;
                        ep.maxParticles = m_MaxParticles;
                        ep.emitCount    = emitCount;

                        CudaInterop::LaunchEmit(m_CudaInterop->GetMappedPointer(m_CudaSlotParticle),
                                                m_CudaInterop->GetMappedPointer(m_CudaSlotDeadList),
                                                m_CudaInterop->GetMappedPointer(m_CudaSlotCounter), ep, stream);
                    }

                    // Simulate
                    {
                        CudaInterop::SimulateParams sp{};
                        sp.deltaTime    = std::min(dt, 0.05f);
                        sp.gravity[0]   = emitter.Gravity.x;
                        sp.gravity[1]   = emitter.Gravity.y;
                        sp.gravity[2]   = emitter.Gravity.z;
                        sp.damping      = emitter.Damping;
                        sp.maxParticles = m_MaxParticles;

                        CudaInterop::LaunchSimulate(m_CudaInterop->GetMappedPointer(m_CudaSlotParticle),
                                                    m_CudaInterop->GetMappedPointer(m_CudaSlotDeadList),
                                                    m_CudaInterop->GetMappedPointer(m_CudaSlotAliveList),
                                                    m_CudaInterop->GetMappedPointer(m_CudaSlotCounter), sp, stream);
                    }

                    // Render Args
                    CudaInterop::LaunchRenderArgs(m_CudaInterop->GetMappedPointer(m_CudaSlotCounter),
                                                  m_CudaInterop->GetMappedPointer(m_CudaSlotIndirect), stream);

                    CudaInterop::RecordCudaEvent(m_CudaTiming.EventStop, stream);
                    m_CudaInterop->UnmapAll();

                    if (CudaInterop::IsCudaPoisoned())
                    {
                        ENGINE_WARN(
                            "[Particle] CUDA poisoned during compute ({}); permanently falling back to GL compute.",
                            CudaInterop::GetCudaPoisonReason());
                        m_UseCudaPath = false;
                    }
                    else
                    {
                        // 延迟查询：读取上一帧的计时结果（此时 GPU 已完成，非阻塞）
                        if (m_CudaTiming.HasPrevTiming && m_CudaTiming.PrevStop)
                        {
                            float ms = CudaInterop::CudaEventElapsedMs(m_CudaTiming.PrevStart, m_CudaTiming.PrevStop);
                            if (ms >= 0.0f)
                                PerformanceMonitor::Get().SetParticleComputeCudaMs(ms);
                        }
                        // 交换事件对：当前帧的 start/stop 变成下帧待查询的 prev
                        m_CudaTiming.SwapEvents();
                        cudaSucceeded = true;
                    }
                }
                else
                {
                    ENGINE_WARN("[Particle] CUDA MapAll failed ({}); permanently falling back to GL compute.",
                                CudaInterop::GetCudaPoisonReason());
                    m_UseCudaPath = false;
                }
            }
        }
        if (!cudaSucceeded)
#endif
        { // GL Compute path
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
                m_EmitShader->SetFloat("u_LifeMin", emitter.LifeMin);
                m_EmitShader->SetFloat("u_LifeMax", emitter.LifeMax);
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

            PerformanceMonitor::Get().GetParticleComputeGPUTimer().End();
        } // GL Compute path

#ifdef ENGINE_ENABLE_CUDA
        PerformanceMonitor::Get().SetParticleUsingCuda(cudaSucceeded);
#endif

        // ---- 异步回读：始终执行，用于 simulate 按存活数 dispatch + SPH + direct-draw ----
        {
            // ---- 先收上一帧的结果（零等待） ----
            if (m_ReadbackPending && m_ReadbackFence)
            {
                GLenum result = glClientWaitSync(static_cast<GLsync>(m_ReadbackFence), GL_SYNC_FLUSH_COMMANDS_BIT, 0);

                if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED)
                {
                    // GPU 已完成，无阻塞读取回读缓冲
                    CounterData counters{};
                    glBindBuffer(GL_COPY_READ_BUFFER, m_ReadbackBuffer);
                    glGetBufferSubData(GL_COPY_READ_BUFFER, 0, sizeof(CounterData), &counters);
                    glBindBuffer(GL_COPY_READ_BUFFER, 0);

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

                    // 始终更新存活数，用于下一帧 simulate dispatch 和 SPH
                    m_LastAliveCount = sanitized.aliveCount;

                    if (!m_UseIndirectDraw)
                        m_AliveCountForDirectDraw = sanitized.aliveCount;

                    m_ReadbackPending = false;
                }
                // GL_TIMEOUT_EXPIRED: GPU 还没完成，跳过本帧回读，用旧值
            }

            // ---- 发起本帧的异步拷贝 ----
            if (m_ReadbackFence)
            {
                glDeleteSync(static_cast<GLsync>(m_ReadbackFence));
                m_ReadbackFence = nullptr;
            }

            // 将 Counter SSBO 拷贝到回读缓冲
            glBindBuffer(GL_COPY_READ_BUFFER, m_CounterBuffer->GetRendererID());
            glBindBuffer(GL_COPY_WRITE_BUFFER, m_ReadbackBuffer);
            glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, sizeof(CounterData));
            glBindBuffer(GL_COPY_READ_BUFFER, 0);
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);

            // 插入栅栏：下一帧检查时拷贝已完成
            m_ReadbackFence   = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            m_ReadbackPending = true;
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
