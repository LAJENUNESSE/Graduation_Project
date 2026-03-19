#include "engpch.h"
#include "Renderer/FluidSystemGPU.h"
#include "Renderer/SPHCommon.h"
#include "Core/Log.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/RendererAPI.h"
#include "Scene/Components.h"

#include <cmath>
#include <glad/gl.h>

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

    // Must match GLSL struct layout: 5 x vec4 = 80 bytes
    struct FluidGPUParticle
    {
        glm::vec4 posAndLife;
        glm::vec4 velAndMaxLife;
        glm::vec4 startColor;
        glm::vec4 endColor;
        glm::vec4 params; // z=density(SPH), w=pressure(SPH)
    };

#ifdef ENGINE_ENABLE_CUDA
    static_assert(sizeof(FluidGPUParticle) == sizeof(CudaInterop::GPUParticle), "FluidGPUParticle size mismatch");
    static_assert(sizeof(GPURigidBodyData) == sizeof(CudaInterop::RigidBodyData), "GPURigidBodyData size mismatch");
    static_assert(offsetof(FluidGPUParticle, posAndLife) == offsetof(CudaInterop::GPUParticle, posAndLife),
                  "posAndLife offset mismatch");
    static_assert(offsetof(FluidGPUParticle, velAndMaxLife) == offsetof(CudaInterop::GPUParticle, velAndMaxLife),
                  "velAndMaxLife offset mismatch");
    static_assert(offsetof(FluidGPUParticle, startColor) == offsetof(CudaInterop::GPUParticle, startColor),
                  "startColor offset mismatch");
    static_assert(offsetof(FluidGPUParticle, endColor) == offsetof(CudaInterop::GPUParticle, endColor),
                  "endColor offset mismatch");
    static_assert(offsetof(FluidGPUParticle, params) == offsetof(CudaInterop::GPUParticle, params),
                  "params offset mismatch");
    static_assert(offsetof(GPURigidBodyData, posAndType) == offsetof(CudaInterop::RigidBodyData, posAndType),
                  "posAndType offset mismatch");
    static_assert(offsetof(GPURigidBodyData, rotCol0) == offsetof(CudaInterop::RigidBodyData, rotCol0),
                  "rotCol0 offset mismatch");
    static_assert(offsetof(GPURigidBodyData, halfExtents) == offsetof(CudaInterop::RigidBodyData, halfExtents),
                  "halfExtents offset mismatch");
    static_assert(offsetof(GPURigidBodyData, linearVel) == offsetof(CudaInterop::RigidBodyData, linearVel),
                  "linearVel offset mismatch");
#endif

    FluidSystemGPU::FluidSystemGPU(uint32_t particleCount) : m_ParticleCount(particleCount) {}

    FluidSystemGPU::~FluidSystemGPU()
    {
#ifdef ENGINE_ENABLE_CUDA
        if (m_CudaSPHCtx)
            CudaInterop::DestroySPHContext(m_CudaSPHCtx);
        m_CudaTiming.Destroy();
#endif
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
        m_ParticleBuffer = ShaderStorageBuffer::CreateGPUOnly(zeroData.data(), totalBytes, 0);

        // Identity alive list: [0, 1, 2, ..., N-1]
        std::vector<uint32_t> aliveIndices(m_ParticleCount);
        for (uint32_t i = 0; i < m_ParticleCount; i++)
            aliveIndices[i] = i;
        m_AliveList = ShaderStorageBuffer::CreateGPUOnly(aliveIndices.data(), m_ParticleCount * sizeof(uint32_t), 2);

        // Empty VAO for instanced draw
        m_EmptyVAO = VertexArray::Create();

        m_Initialized = true;

#ifdef ENGINE_ENABLE_CUDA
        if (!m_CudaInitAttempted)
        {
            m_CudaInitAttempted = true;
            if (!CudaInterop::IsCudaPoisoned() && CudaGLInteropContext::ProbeDeviceMatch())
            {
                m_CudaInterop = CreateScope<CudaGLInteropContext>();
                m_CudaSlotParticle =
                    m_CudaInterop->RegisterBuffer(m_ParticleBuffer->GetRendererID(), "FluidParticleBuffer");
                if (m_CudaSlotParticle >= 0)
                {
                    m_CudaSPHCtx = CudaInterop::CreateSPHContext(m_ParticleCount, 64, MAX_RIGID_BODIES);
                    if (m_CudaSPHCtx)
                    {
                        m_CudaTiming.Init();
                        m_UseCudaPath = true;
                        ENGINE_INFO("[Fluid] CUDA SPH sidecar activated.");
                    }
                    else
                    {
                        ENGINE_WARN("[Fluid] CUDA CreateSPHContext failed (out of memory?), staying on GL path.");
                    }
                }
                else
                {
                    ENGINE_WARN("[Fluid] CUDA RegisterBuffer failed (slot=-1), staying on GL path.");
                }
            }
            else
            {
                ENGINE_WARN("[Fluid] CUDA ProbeDeviceMatch failed or CUDA poisoned ({}), staying on GL path.",
                            CudaInterop::GetCudaPoisonReason());
            }
        }
#endif
    }

    void FluidSystemGPU::InitSPH(float smoothingRadius)
    {
        if (m_SPHInitialized)
            return;

        float    cellSize = 2.0f * smoothingRadius;
        uint32_t gridSize = 64;
        m_Grid.Init(m_ParticleCount, gridSize, cellSize);
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

#ifdef ENGINE_ENABLE_CUDA
        bool cudaSucceeded = false;
        if (m_UseCudaPath && !CudaInterop::IsCudaPoisoned())
        {
            if (m_CudaInterop->MapAll())
            {
                void* stream       = m_CudaInterop->GetStream();
                void* devParticles = m_CudaInterop->GetMappedPointer(m_CudaSlotParticle);
                CudaInterop::RecordCudaEvent(m_CudaTiming.EventStart, stream);

                float cellSize = 2.0f * kp.h;

                // Grid Build（内部用 CUB prefix sum，替代 3 dispatch）
                CudaInterop::LaunchSPHGridBuild(m_CudaSPHCtx, devParticles, m_ParticleCount, 64, cellSize, stream);

                // 构造 SPHParams
                CudaInterop::SPHParams p{};
                p.smoothingRadius = kp.h;
                p.poly6Coeff      = kp.poly6Coeff;
                p.spikyCoeff      = kp.spikyCoeff;
                p.particleMass    = emitter.ParticleMass;
                p.restDensity     = emitter.RestDensity;
                p.gasConstant     = emitter.GasConstant;
                p.viscosity       = emitter.Viscosity;
                p.surfaceTension  = emitter.SurfaceTension;
                p.deltaTime       = clampedDt;
                p.gridSize        = 64;
                p.cellSize        = cellSize;
                p.aliveCount      = static_cast<int>(m_ParticleCount);
                p.gravity[0]      = emitter.Gravity.x;
                p.gravity[1]      = emitter.Gravity.y;
                p.gravity[2]      = emitter.Gravity.z;
                p.warmupTime      = 0.0f; // 流体粒子永久存活，不需要 warmup

                // 刚体数据：CPU→CUDA 每帧上传（WCSPH/PCISPH 共用）
                uint32_t cudaRigidBodyCount = 0;
                if (emitter.RigidBodyCoupling && registry)
                {
                    std::vector<GPURigidBodyData> bodies;
                    bodies.reserve(MAX_RIGID_BODIES);
                    auto boxView = registry->view<TransformComponent, BoxColliderComponent>();
                    for (auto entity : boxView)
                    {
                        if (bodies.size() >= MAX_RIGID_BODIES)
                            break;
                        auto&     tc     = boxView.get<TransformComponent>(entity);
                        auto&     bc     = boxView.get<BoxColliderComponent>(entity);
                        glm::quat rot    = glm::quat(tc.Rotation);
                        glm::mat4 rotMat = glm::toMat4(rot);
                        glm::vec3 absScale =
                            glm::vec3(std::abs(tc.Scale.x), std::abs(tc.Scale.y), std::abs(tc.Scale.z));
                        GPURigidBodyData body{};
                        body.posAndType  = glm::vec4(tc.Translation + rot * (bc.Offset * tc.Scale), 0.0f);
                        body.rotCol0     = glm::vec4(rotMat[0][0], rotMat[0][1], rotMat[0][2], 0.0f);
                        body.rotCol1     = glm::vec4(rotMat[1][0], rotMat[1][1], rotMat[1][2], 0.0f);
                        body.rotCol2     = glm::vec4(rotMat[2][0], rotMat[2][1], rotMat[2][2], 0.0f);
                        body.halfExtents = glm::vec4(bc.HalfExtents * absScale, 0.0f);
                        if (registry->all_of<RigidBodyComponent>(entity))
                        {
                            auto& rb        = registry->get<RigidBodyComponent>(entity);
                            body.linearVel  = glm::vec4(rb.LinearVelocity, 0.0f);
                            body.angularVel = glm::vec4(rb.AngularVelocity, 0.0f);
                        }
                        bodies.push_back(body);
                    }
                    auto sphereView = registry->view<TransformComponent, SphereColliderComponent>();
                    for (auto entity : sphereView)
                    {
                        if (bodies.size() >= MAX_RIGID_BODIES)
                            break;
                        auto&            tc     = sphereView.get<TransformComponent>(entity);
                        auto&            sc     = sphereView.get<SphereColliderComponent>(entity);
                        glm::quat        rot    = glm::quat(tc.Rotation);
                        glm::mat4        rotMat = glm::toMat4(rot);
                        GPURigidBodyData body{};
                        body.posAndType  = glm::vec4(tc.Translation + rot * (sc.Offset * tc.Scale), 1.0f);
                        body.rotCol0     = glm::vec4(rotMat[0][0], rotMat[0][1], rotMat[0][2], 0.0f);
                        body.rotCol1     = glm::vec4(rotMat[1][0], rotMat[1][1], rotMat[1][2], 0.0f);
                        body.rotCol2     = glm::vec4(rotMat[2][0], rotMat[2][1], rotMat[2][2], 0.0f);
                        float maxScale   = std::max({std::abs(tc.Scale.x), std::abs(tc.Scale.y), std::abs(tc.Scale.z)});
                        body.halfExtents = glm::vec4(sc.Radius * maxScale, 0.0f, 0.0f, 0.0f);
                        if (registry->all_of<RigidBodyComponent>(entity))
                        {
                            auto& rb        = registry->get<RigidBodyComponent>(entity);
                            body.linearVel  = glm::vec4(rb.LinearVelocity, 0.0f);
                            body.angularVel = glm::vec4(rb.AngularVelocity, 0.0f);
                        }
                        bodies.push_back(body);
                    }
                    cudaRigidBodyCount = static_cast<uint32_t>(bodies.size());
                    if (!bodies.empty())
                        CudaInterop::SPHUploadRigidBodies(m_CudaSPHCtx, bodies.data(), cudaRigidBodyCount);
                }

                // SPH Density
                CudaInterop::LaunchSPHDensity(m_CudaSPHCtx, devParticles, p, stream);

                CudaInterop::PCISPHIterParams ip{};
                ip.pcisphDelta       = emitter.PCISPHDelta;
                ip.boundaryStiffness = emitter.BoundaryStiffness;
                ip.boundaryDamping   = emitter.BoundaryDamping;
                ip.rigidBodyCount    = static_cast<int>(cudaRigidBodyCount);

                if (emitter.PCISPHEnabled)
                {
                    CudaInterop::LaunchPCISPHInit(m_CudaSPHCtx, devParticles, p, stream);

                    int iterations = std::clamp(emitter.PCISPHIterations, 1, 8);
                    for (int iter = 0; iter < iterations; ++iter)
                    {
                        // 迭代 1+：用预测位置重建 grid
                        if (iter > 0)
                        {
                            CudaInterop::LaunchSPHGridBuild(m_CudaSPHCtx, devParticles, m_ParticleCount, 64, cellSize,
                                                            stream, true);
                        }
                        CudaInterop::LaunchPCISPHPredict(m_CudaSPHCtx, devParticles, clampedDt,
                                                         static_cast<int>(m_ParticleCount), stream);
                        CudaInterop::LaunchPCISPHDensity(m_CudaSPHCtx, devParticles, p, ip, stream);
                        CudaInterop::LaunchPCISPHForce(m_CudaSPHCtx, devParticles, p, ip, stream);
                    }
                    CudaInterop::LaunchPCISPHApply(m_CudaSPHCtx, devParticles, static_cast<int>(m_ParticleCount),
                                                   stream);
                }
                else
                {
                    CudaInterop::LaunchSPHForce(m_CudaSPHCtx, devParticles, p, ip, stream);
                }

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
                CudaInterop::LaunchSPHSimulate(devParticles, sp, stream);

                CudaInterop::RecordCudaEvent(m_CudaTiming.EventStop, stream);
                m_CudaInterop->UnmapAll();

                if (CudaInterop::IsCudaPoisoned())
                {
                    ENGINE_WARN("[Fluid] CUDA poisoned during compute ({}); falling back to GL.",
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
                            PerformanceMonitor::Get().SetFluidComputeCudaMs(ms);
                    }
                    // 交换事件对：当前帧的 start/stop 变成下帧待查询的 prev
                    m_CudaTiming.SwapEvents();
                    cudaSucceeded = true;
                }
            }
            else
            {
                ENGINE_WARN("[Fluid] CUDA MapAll failed ({}); falling back to GL.", CudaInterop::GetCudaPoisonReason());
                m_UseCudaPath = false;
            }
        }
        if (!cudaSucceeded)
#endif
        { // ---- GL Compute 路径（原有代码）----
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
            m_SPHShaders.DensityShader->Bind();
            m_SPHShaders.DensityShader->SetInt("u_AliveCount", static_cast<int>(m_ParticleCount));
            m_SPHShaders.DensityShader->SetFloat("u_SmoothingRadius", kp.h);
            m_SPHShaders.DensityShader->SetFloat("u_ParticleMass", emitter.ParticleMass);
            m_SPHShaders.DensityShader->SetFloat("u_RestDensity", emitter.RestDensity);
            m_SPHShaders.DensityShader->SetFloat("u_GasConstant", emitter.GasConstant);
            m_SPHShaders.DensityShader->SetInt("u_GridSize", gridSize);
            m_SPHShaders.DensityShader->SetFloat("u_CellSize", cellSize);
            m_SPHShaders.DensityShader->SetFloat("u_Poly6Coeff", kp.poly6Coeff);

            uint32_t sphGroups = (m_ParticleCount + 255) / 256;
            RenderCommand::DispatchCompute(sphGroups);
            RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

            if (emitter.PCISPHEnabled)
            {
                // --- PCISPH path ---
                InitPCISPH();

                uint32_t rigidBodyCount = 0;
                if (emitter.RigidBodyCoupling && registry)
                {
                    InitRigidBodyBuffer();
                    rigidBodyCount = UploadRigidBodiesToBuffer(registry, m_RigidBodyBuffer, MAX_RIGID_BODIES,
                                                               RigidBodyUploadFilter::AllColliders);
                }

                m_PCISPHBuffer->Bind(1);
                if (m_RigidBodyBuffer)
                    m_RigidBodyBuffer->Bind(3);

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
                    m_SPHShaders.PCISPHDensity->SetFloat("u_PCISPHDelta", emitter.PCISPHDelta);
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
                if (emitter.RigidBodyCoupling && registry)
                {
                    InitRigidBodyBuffer();
                    rigidBodyCount = UploadRigidBodiesToBuffer(registry, m_RigidBodyBuffer, MAX_RIGID_BODIES,
                                                               RigidBodyUploadFilter::AllColliders);
                    m_RigidBodyBuffer->Bind(3);
                }
                m_SPHShaders.ForceShader->SetInt("u_RigidBodyCount", static_cast<int>(rigidBodyCount));
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
        } // end if (!cudaSucceeded)

        PerformanceMonitor::Get().SetFluidUsingCuda(
#ifdef ENGINE_ENABLE_CUDA
            cudaSucceeded
#else
            false
#endif
        );
        PerformanceMonitor::Get().SetFluidActive(true);
    }

} // namespace Engine
