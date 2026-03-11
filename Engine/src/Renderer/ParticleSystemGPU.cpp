#include "engpch.h"
#include "Renderer/ParticleSystemGPU.h"
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

    // Must match GPU GPURigidBody struct: 7 x vec4 = 112 bytes
    struct GPURigidBodyData
    {
        glm::vec4 posAndType; // xyz=center, w=0(box)/1(sphere)
        glm::vec4 rotCol0;
        glm::vec4 rotCol1;
        glm::vec4 rotCol2;
        glm::vec4 halfExtents; // box: xyz=半尺寸; sphere: x=radius
        glm::vec4 linearVel;
        glm::vec4 angularVel;
    };

    ParticleSystemGPU::ParticleSystemGPU(uint32_t maxParticles) : m_MaxParticles(maxParticles) {}

    ParticleSystemGPU::~ParticleSystemGPU()
    {
#ifdef ENGINE_ENABLE_CUDA
        if (m_CudaEventStart)
            CudaInterop::DestroyCudaEvent(m_CudaEventStart);
        if (m_CudaEventStop)
            CudaInterop::DestroyCudaEvent(m_CudaEventStop);
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
        m_EmitShader = Shader::Create("assets/shaders/particle_emit.glsl");
        m_SimulateShader = Shader::Create("assets/shaders/particle_simulate.glsl");
        m_RenderArgsShader = Shader::Create("assets/shaders/particle_render_args.glsl");
        m_BillboardShader = Shader::Create("assets/shaders/particle_billboard.glsl");

        // SPH shaders (loaded eagerly, only dispatched when SPHEnabled)
        m_SPHDensityShader = Shader::Create("assets/shaders/sph_density.glsl");
        m_SPHForceShader = Shader::Create("assets/shaders/sph_force.glsl");

        // PCISPH shaders
        m_PCISPHInitShader = Shader::Create("assets/shaders/sph_pcisph_init.glsl");
        m_PCISPHPredictShader = Shader::Create("assets/shaders/sph_pcisph_predict.glsl");
        m_PCISPHDensityShader = Shader::Create("assets/shaders/sph_pcisph_density.glsl");
        m_PCISPHForceShader = Shader::Create("assets/shaders/sph_pcisph_force.glsl");
        m_PCISPHApplyShader = Shader::Create("assets/shaders/sph_pcisph_apply.glsl");

        // Allocate particle pool — MUST be zero-initialized so all particles
        // start with life=0.0 (properly dead). Undefined buffer data may contain
        // NaN or positive life values, causing simulate to treat uninitialized
        // particles as alive and permanently corrupting the dead/alive counters.
        // GPU-only immutable storage: after init, only GPU reads/writes this buffer.
        uint32_t particleSize = sizeof(GPUParticleData); // 80 bytes
        uint32_t totalBytes = m_MaxParticles * particleSize;
        std::vector<uint8_t> zeroData(totalBytes, 0);
        m_ParticleBuffer = ShaderStorageBuffer::CreateGPUOnly(zeroData.data(), totalBytes, 0);

        // Fill dead list with indices [0, 1, 2, ..., MAX-1]
        std::vector<uint32_t> deadIndices(m_MaxParticles);
        for (uint32_t i = 0; i < m_MaxParticles; i++)
            deadIndices[i] = i;
        m_DeadList = ShaderStorageBuffer::CreateGPUOnly(deadIndices.data(), m_MaxParticles * sizeof(uint32_t), 1);

        // Alive list (empty at start)
        m_AliveList = ShaderStorageBuffer::CreateGPUOnly(m_MaxParticles * sizeof(uint32_t), 2);

        // Counter buffer: deadCount=MAX, aliveCount=0, emitCount=0, pad=0
        // Kept as DYNAMIC — CPU writes aliveCount/emitCount every frame
        CounterData counters{m_MaxParticles, 0, 0, 0};
        m_CounterBuffer = ShaderStorageBuffer::Create(&counters, sizeof(CounterData), 3);

        // Indirect draw args: count=6, instanceCount=0, first=0, baseInstance=0
        IndirectDrawCommand cmd{6, 0, 0, 0};
        m_IndirectArgs = ShaderStorageBuffer::CreateGPUOnly(&cmd, sizeof(IndirectDrawCommand), 4);

        // Empty VAO for billboard rendering
        m_EmptyVAO = VertexArray::Create();

        // 异步回读缓冲：用于避免 glGetBufferSubData 的同步阻塞
        glGenBuffers(1, &m_ReadbackBuffer);
        glBindBuffer(GL_COPY_WRITE_BUFFER, m_ReadbackBuffer);
        glBufferData(GL_COPY_WRITE_BUFFER, sizeof(CounterData), nullptr, GL_STREAM_READ);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);

        const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));

        // VMware SVGA has known instability with advanced compute/indirect paths.
        bool vmwareDriver = ContainsToken(vendor, "VMware") || ContainsToken(renderer, "SVGA3D");
        m_VMwareCompatMode = vmwareDriver;

        const char* forceDirect = std::getenv("ENGINE_PARTICLE_DIRECT_DRAW");
        bool envForceDirect = forceDirect && forceDirect[0] == '1';

        m_UseIndirectDraw = !(vmwareDriver || envForceDirect);
        if (!m_UseIndirectDraw)
        {
            ENGINE_WARN("[Particle] Using direct instanced draw fallback (VMware compatibility mode).");
        }

        const char* allowSPHEnv = std::getenv("ENGINE_ENABLE_SPH_ON_VMWARE");
        bool allowSPHOnVMware = allowSPHEnv && allowSPHEnv[0] == '1';
        m_DisableSPHOnDriver = vmwareDriver && !allowSPHOnVMware;
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
            if (CudaGLInteropContext::ProbeDeviceMatch())
            {
                m_CudaInterop = CreateScope<CudaGLInteropContext>();
                m_CudaSlotParticle = m_CudaInterop->RegisterBuffer(m_ParticleBuffer->GetRendererID(), "ParticleBuffer");
                m_CudaSlotDeadList = m_CudaInterop->RegisterBuffer(m_DeadList->GetRendererID(), "DeadList");
                m_CudaSlotAliveList = m_CudaInterop->RegisterBuffer(m_AliveList->GetRendererID(), "AliveList");
                m_CudaSlotCounter = m_CudaInterop->RegisterBuffer(m_CounterBuffer->GetRendererID(), "CounterBuffer");
                m_CudaSlotIndirect = m_CudaInterop->RegisterBuffer(m_IndirectArgs->GetRendererID(), "IndirectArgs");

                if (m_CudaSlotParticle >= 0 && m_CudaSlotDeadList >= 0 && m_CudaSlotAliveList >= 0 &&
                    m_CudaSlotCounter >= 0 && m_CudaSlotIndirect >= 0)
                {
                    m_UseCudaPath = true;
                    m_CudaEventStart = CudaInterop::CreateCudaEvent();
                    m_CudaEventStop = CudaInterop::CreateCudaEvent();
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
        float cellSize = 2.0f * smoothingRadius;
        uint32_t gridSize = 64;

        m_Grid.Init(m_MaxParticles, gridSize, cellSize);
        m_SPHInitialized = true;
    }

    void ParticleSystemGPU::InitPCISPH()
    {
        if (m_PCISPHInitialized)
            return;
        // PCISPHData: 3 × vec4 = 48 bytes per particle
        m_PCISPHBuffer = ShaderStorageBuffer::CreateGPUOnly(m_MaxParticles * 48, 1);
        m_PCISPHInitialized = true;
    }

    void ParticleSystemGPU::InitRigidBodyBuffer()
    {
        if (m_RigidBodyBuffer)
            return;
        m_RigidBodyBuffer = ShaderStorageBuffer::Create(MAX_RIGID_BODIES * sizeof(GPURigidBodyData), 3);
    }

    uint32_t ParticleSystemGPU::UploadRigidBodies(entt::registry* registry)
    {
        if (!registry || !m_RigidBodyBuffer)
            return 0;

        std::vector<GPURigidBodyData> bodies;
        bodies.reserve(MAX_RIGID_BODIES);

        auto view = registry->view<TransformComponent, RigidBodyComponent>();
        for (auto entity : view)
        {
            if (bodies.size() >= MAX_RIGID_BODIES)
                break;

            auto& tc = view.get<TransformComponent>(entity);
            // RigidBodyComponent required for view filter but not directly accessed

            // 构建旋转矩阵
            glm::mat4 rotMat = glm::toMat4(glm::quat(tc.Rotation));

            GPURigidBodyData body{};
            body.rotCol0 = glm::vec4(rotMat[0][0], rotMat[0][1], rotMat[0][2], 0.0f);
            body.rotCol1 = glm::vec4(rotMat[1][0], rotMat[1][1], rotMat[1][2], 0.0f);
            body.rotCol2 = glm::vec4(rotMat[2][0], rotMat[2][1], rotMat[2][2], 0.0f);
            body.linearVel = glm::vec4(0.0f);
            body.angularVel = glm::vec4(0.0f);

            if (registry->all_of<BoxColliderComponent>(entity))
            {
                auto& bc = registry->get<BoxColliderComponent>(entity);
                body.posAndType = glm::vec4(tc.Translation + bc.Offset, 0.0f); // w=0 for box
                body.halfExtents = glm::vec4(bc.HalfExtents * tc.Scale, 0.0f);
                bodies.push_back(body);
            }
            else if (registry->all_of<SphereColliderComponent>(entity))
            {
                auto& sc = registry->get<SphereColliderComponent>(entity);
                body.posAndType = glm::vec4(tc.Translation + sc.Offset, 1.0f); // w=1 for sphere
                float maxScale = std::max({tc.Scale.x, tc.Scale.y, tc.Scale.z});
                body.halfExtents = glm::vec4(sc.Radius * maxScale, 0.0f, 0.0f, 0.0f);
                bodies.push_back(body);
            }
        }

        if (!bodies.empty())
            m_RigidBodyBuffer->SetData(bodies.data(), static_cast<uint32_t>(bodies.size() * sizeof(GPURigidBodyData)));

        return static_cast<uint32_t>(bodies.size());
    }

    void ParticleSystemGPU::Update(float dt, const glm::vec3& emitterPos, const ParticleEmitterComponent& emitter,
                                   entt::registry* registry)
    {
        if (!m_Initialized)
            return;

        const bool sphEnabled = emitter.SPHEnabled && !m_DisableSPHOnDriver;
        if (emitter.SPHEnabled && m_DisableSPHOnDriver && !m_SPHDisableLogged)
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
        if (m_UseCudaPath && !sphEnabled)
        {
            if (m_CudaInterop->MapAll())
            {
                void* stream = m_CudaInterop->GetStream();
                CudaInterop::RecordCudaEvent(m_CudaEventStart, stream);

                // Emit
                if (emitCount > 0)
                {
                    CudaInterop::EmitParams ep{};
                    ep.emitterPos[0] = emitterPos.x;
                    ep.emitterPos[1] = emitterPos.y;
                    ep.emitterPos[2] = emitterPos.z;
                    ep.emitDirection[0] = emitter.EmitDirection.x;
                    ep.emitDirection[1] = emitter.EmitDirection.y;
                    ep.emitDirection[2] = emitter.EmitDirection.z;
                    ep.emitAngle = glm::radians(emitter.EmitAngle);
                    ep.lifeMin = emitter.LifeMin;
                    ep.lifeMax = emitter.LifeMax;
                    ep.speedMin = emitter.SpeedMin;
                    ep.speedMax = emitter.SpeedMax;
                    ep.sizeStart = emitter.SizeStart;
                    ep.sizeEnd = emitter.SizeEnd;
                    ep.startColor[0] = emitter.ColorStart.r;
                    ep.startColor[1] = emitter.ColorStart.g;
                    ep.startColor[2] = emitter.ColorStart.b;
                    ep.startColor[3] = emitter.ColorStart.a;
                    ep.endColor[0] = emitter.ColorEnd.r;
                    ep.endColor[1] = emitter.ColorEnd.g;
                    ep.endColor[2] = emitter.ColorEnd.b;
                    ep.endColor[3] = emitter.ColorEnd.a;
                    m_TotalTime += dt;
                    ep.time = m_TotalTime;
                    ep.maxParticles = m_MaxParticles;
                    ep.emitCount = emitCount;

                    CudaInterop::LaunchEmit(m_CudaInterop->GetMappedPointer(m_CudaSlotParticle),
                                            m_CudaInterop->GetMappedPointer(m_CudaSlotDeadList),
                                            m_CudaInterop->GetMappedPointer(m_CudaSlotCounter), ep, stream);
                }

                // Simulate
                {
                    CudaInterop::SimulateParams sp{};
                    sp.deltaTime = std::min(dt, 0.05f);
                    sp.gravity[0] = emitter.Gravity.x;
                    sp.gravity[1] = emitter.Gravity.y;
                    sp.gravity[2] = emitter.Gravity.z;
                    sp.damping = emitter.Damping;
                    sp.maxParticles = m_MaxParticles;

                    CudaInterop::LaunchSimulate(m_CudaInterop->GetMappedPointer(m_CudaSlotParticle),
                                                m_CudaInterop->GetMappedPointer(m_CudaSlotDeadList),
                                                m_CudaInterop->GetMappedPointer(m_CudaSlotAliveList),
                                                m_CudaInterop->GetMappedPointer(m_CudaSlotCounter), sp, stream);
                }

                // Render Args
                CudaInterop::LaunchRenderArgs(m_CudaInterop->GetMappedPointer(m_CudaSlotCounter),
                                              m_CudaInterop->GetMappedPointer(m_CudaSlotIndirect), stream);

                CudaInterop::RecordCudaEvent(m_CudaEventStop, stream);
                m_CudaInterop->UnmapAll();

                float cudaMs = CudaInterop::CudaEventElapsedMs(m_CudaEventStart, m_CudaEventStop);
                PerformanceMonitor::Get().SetParticleComputeCudaMs(cudaMs);
                cudaSucceeded = true;
            }
            else
            {
                ENGINE_WARN("[Particle] CUDA MapAll failed; permanently falling back to GL compute.");
                m_UseCudaPath = false;
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

            // ---- SPH passes (only when SPHEnabled) ----
            if (sphEnabled)
            {
                // Lazy-init spatial hash grid
                if (!m_SPHInitialized)
                    InitSPH(emitter.SPH_SmoothingRadius);

                // 用上一帧的活跃粒子数做 SPH dispatch（本帧 aliveCount 还没建好）
                // 首帧 m_LastAliveCount=0 会跳过 SPH，第二帧开始正常
                if (m_LastAliveCount > 0)
                {
                    float cellSize = m_Grid.GetCellSize();
                    int gridSize = static_cast<int>(m_Grid.GetGridSize());

                    // CPU 侧预计算 SPH kernel 常量（避免 GPU 每粒子每邻居重复计算）
                    float h = emitter.SPH_SmoothingRadius;
                    float h2 = h * h;
                    float h6 = h2 * h2 * h2;
                    float h9 = h6 * h2 * h;
                    float poly6Coeff = 315.0f / (64.0f * glm::pi<float>() * h9);
                    float spikyCoeff = -45.0f / (glm::pi<float>() * h6);

                    // Pass 2a: Build Spatial Hash Grid
                    m_Grid.Build(m_LastAliveCount);

                    // Pass 2b: SPH Density
                    m_SPHDensityShader->Bind();
                    m_SPHDensityShader->SetInt("u_AliveCount", static_cast<int>(m_LastAliveCount));
                    m_SPHDensityShader->SetFloat("u_SmoothingRadius", emitter.SPH_SmoothingRadius);
                    m_SPHDensityShader->SetFloat("u_ParticleMass", emitter.SPH_ParticleMass);
                    m_SPHDensityShader->SetFloat("u_RestDensity", emitter.SPH_RestDensity);
                    m_SPHDensityShader->SetFloat("u_GasConstant", emitter.SPH_GasConstant);
                    m_SPHDensityShader->SetInt("u_GridSize", gridSize);
                    m_SPHDensityShader->SetFloat("u_CellSize", cellSize);
                    m_SPHDensityShader->SetFloat("u_Poly6Coeff", poly6Coeff);

                    uint32_t sphGroups = (m_LastAliveCount + 255) / 256;
                    RenderCommand::DispatchCompute(sphGroups);
                    RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                    if (emitter.SPH_PCISPHEnabled)
                    {
                        // ---- PCISPH 路径 ----
                        InitPCISPH();

                        uint32_t rigidBodyCount = 0;
                        if (emitter.SPH_RigidBodyCoupling && registry)
                        {
                            InitRigidBodyBuffer();
                            rigidBodyCount = UploadRigidBodies(registry);
                        }

                        m_PCISPHBuffer->Bind(1);
                        if (m_RigidBodyBuffer)
                            m_RigidBodyBuffer->Bind(3);

                        // PCISPH Init
                        m_PCISPHInitShader->Bind();
                        m_PCISPHInitShader->SetInt("u_AliveCount", static_cast<int>(m_LastAliveCount));
                        m_PCISPHInitShader->SetFloat("u_SmoothingRadius", emitter.SPH_SmoothingRadius);
                        m_PCISPHInitShader->SetFloat("u_ParticleMass", emitter.SPH_ParticleMass);
                        m_PCISPHInitShader->SetFloat("u_Viscosity", emitter.SPH_Viscosity);
                        m_PCISPHInitShader->SetFloat("u_DeltaTime", clampedDt);
                        m_PCISPHInitShader->SetInt("u_GridSize", gridSize);
                        m_PCISPHInitShader->SetFloat("u_CellSize", cellSize);
                        m_PCISPHInitShader->SetFloat3("u_Gravity", emitter.Gravity);
                        m_PCISPHInitShader->SetFloat("u_SurfaceTension", emitter.SPH_SurfaceTension);
                        m_PCISPHInitShader->SetFloat("u_SpikyCoeff", spikyCoeff);
                        RenderCommand::DispatchCompute(sphGroups);
                        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                        // 循环前一次性设置所有三个 shader 的 uniform（跨 glUseProgram 保持）
                        int iterations = std::clamp(emitter.SPH_PCISPHIterations, 1, 8);

                        m_PCISPHPredictShader->Bind();
                        m_PCISPHPredictShader->SetInt("u_AliveCount", static_cast<int>(m_LastAliveCount));
                        m_PCISPHPredictShader->SetFloat("u_DeltaTime", clampedDt);

                        m_PCISPHDensityShader->Bind();
                        m_PCISPHDensityShader->SetInt("u_AliveCount", static_cast<int>(m_LastAliveCount));
                        m_PCISPHDensityShader->SetFloat("u_SmoothingRadius", emitter.SPH_SmoothingRadius);
                        m_PCISPHDensityShader->SetFloat("u_ParticleMass", emitter.SPH_ParticleMass);
                        m_PCISPHDensityShader->SetFloat("u_RestDensity", emitter.SPH_RestDensity);
                        m_PCISPHDensityShader->SetFloat("u_PCISPHDelta", emitter.SPH_PCISPHDelta);
                        m_PCISPHDensityShader->SetInt("u_GridSize", gridSize);
                        m_PCISPHDensityShader->SetFloat("u_CellSize", cellSize);
                        m_PCISPHDensityShader->SetFloat("u_Poly6Coeff", poly6Coeff);

                        m_PCISPHForceShader->Bind();
                        m_PCISPHForceShader->SetInt("u_AliveCount", static_cast<int>(m_LastAliveCount));
                        m_PCISPHForceShader->SetFloat("u_SmoothingRadius", emitter.SPH_SmoothingRadius);
                        m_PCISPHForceShader->SetFloat("u_ParticleMass", emitter.SPH_ParticleMass);
                        m_PCISPHForceShader->SetFloat("u_DeltaTime", clampedDt);
                        m_PCISPHForceShader->SetInt("u_GridSize", gridSize);
                        m_PCISPHForceShader->SetFloat("u_CellSize", cellSize);
                        m_PCISPHForceShader->SetInt("u_RigidBodyCount", static_cast<int>(rigidBodyCount));
                        m_PCISPHForceShader->SetFloat("u_BoundaryStiffness", emitter.SPH_BoundaryStiffness);
                        m_PCISPHForceShader->SetFloat("u_BoundaryDamping", emitter.SPH_BoundaryDamping);
                        m_PCISPHForceShader->SetFloat("u_SpikyCoeff", spikyCoeff);
                        m_PCISPHForceShader->SetFloat("u_WarmupTime", SPH_WARMUP_TIME);

                        // 帧间分摊：每帧只做 1 次 predict→density→force 迭代
                        m_PCISPHPredictShader->Bind();
                        RenderCommand::DispatchCompute(sphGroups);
                        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                        m_PCISPHDensityShader->Bind();
                        RenderCommand::DispatchCompute(sphGroups);
                        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                        m_PCISPHForceShader->Bind();
                        RenderCommand::DispatchCompute(sphGroups);
                        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                        m_PCISPHIterationIndex++;
                        if (m_PCISPHIterationIndex >= iterations)
                        {
                            // 最后一次迭代完成，apply v* → particle.vel
                            m_PCISPHApplyShader->Bind();
                            m_PCISPHApplyShader->SetInt("u_AliveCount", static_cast<int>(m_LastAliveCount));
                            RenderCommand::DispatchCompute(sphGroups);
                            RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                            m_PCISPHIterationIndex = 0;
                        }
                    }
                    else
                    {
                        // ---- 现有 WCSPH 路径: SPH Force ----
                        m_SPHForceShader->Bind();
                        m_SPHForceShader->SetInt("u_AliveCount", static_cast<int>(m_LastAliveCount));
                        m_SPHForceShader->SetFloat("u_SmoothingRadius", emitter.SPH_SmoothingRadius);
                        m_SPHForceShader->SetFloat("u_ParticleMass", emitter.SPH_ParticleMass);
                        m_SPHForceShader->SetFloat("u_Viscosity", emitter.SPH_Viscosity);
                        m_SPHForceShader->SetFloat("u_DeltaTime", clampedDt);
                        m_SPHForceShader->SetInt("u_GridSize", gridSize);
                        m_SPHForceShader->SetFloat("u_CellSize", cellSize);
                        // 表面张力 + 刚体耦合 uniform
                        m_SPHForceShader->SetFloat("u_SurfaceTension", emitter.SPH_SurfaceTension);
                        m_SPHForceShader->SetFloat("u_SpikyCoeff", spikyCoeff);
                        m_SPHForceShader->SetFloat("u_WarmupTime", SPH_WARMUP_TIME);

                        uint32_t rigidBodyCount = 0;
                        if (emitter.SPH_RigidBodyCoupling && registry)
                        {
                            InitRigidBodyBuffer();
                            rigidBodyCount = UploadRigidBodies(registry);
                            m_RigidBodyBuffer->Bind(3);
                        }
                        m_SPHForceShader->SetInt("u_RigidBodyCount", static_cast<int>(rigidBodyCount));
                        m_SPHForceShader->SetFloat("u_BoundaryStiffness", emitter.SPH_BoundaryStiffness);
                        m_SPHForceShader->SetFloat("u_BoundaryDamping", emitter.SPH_BoundaryDamping);

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
            // 使用上一帧回读的存活数来优化 dispatch，避免在粒子很少时仍调度全部 workgroup
            uint32_t aliveEstimate = m_LastAliveCount + emitCount;
            if (aliveEstimate > 0)
            {
                // PCISPH handles gravity internally, so pass zero gravity to simulate pass
                glm::vec3 simGravity = (sphEnabled && emitter.SPH_PCISPHEnabled) ? glm::vec3(0.0f) : emitter.Gravity;
                float simulateDt = std::min(dt, 0.05f);
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
                    bool corrected = false;

                    if (sanitized.deadCount > m_MaxParticles)
                    {
                        sanitized.deadCount = m_MaxParticles;
                        corrected = true;
                    }
                    if (sanitized.aliveCount > m_MaxParticles)
                    {
                        sanitized.aliveCount = m_MaxParticles;
                        corrected = true;
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
            m_ReadbackFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
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
