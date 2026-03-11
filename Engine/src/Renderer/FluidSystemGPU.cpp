#include "engpch.h"
#include "Renderer/FluidSystemGPU.h"
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
#endif

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

    // Must match GPU GPURigidBody struct: 7 x vec4 = 112 bytes
    struct FluidGPURigidBody
    {
        glm::vec4 posAndType;
        glm::vec4 rotCol0;
        glm::vec4 rotCol1;
        glm::vec4 rotCol2;
        glm::vec4 halfExtents;
        glm::vec4 linearVel;
        glm::vec4 angularVel;
    };

#ifdef ENGINE_ENABLE_CUDA
    static_assert(sizeof(FluidGPUParticle) == sizeof(CudaInterop::GPUParticle), "FluidGPUParticle size mismatch");
    static_assert(sizeof(FluidGPURigidBody) == sizeof(CudaInterop::RigidBodyData), "FluidGPURigidBody size mismatch");
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
    static_assert(offsetof(FluidGPURigidBody, posAndType) == offsetof(CudaInterop::RigidBodyData, posAndType),
                  "posAndType offset mismatch");
    static_assert(offsetof(FluidGPURigidBody, rotCol0) == offsetof(CudaInterop::RigidBodyData, rotCol0),
                  "rotCol0 offset mismatch");
    static_assert(offsetof(FluidGPURigidBody, halfExtents) == offsetof(CudaInterop::RigidBodyData, halfExtents),
                  "halfExtents offset mismatch");
    static_assert(offsetof(FluidGPURigidBody, linearVel) == offsetof(CudaInterop::RigidBodyData, linearVel),
                  "linearVel offset mismatch");
#endif

    FluidSystemGPU::FluidSystemGPU(uint32_t particleCount) : m_ParticleCount(particleCount) {}

    FluidSystemGPU::~FluidSystemGPU()
    {
#ifdef ENGINE_ENABLE_CUDA
        if (m_CudaSPHCtx)
            CudaInterop::DestroySPHContext(m_CudaSPHCtx);
        if (m_CudaEventStart)
            CudaInterop::DestroyCudaEvent(m_CudaEventStart);
        if (m_CudaEventStop)
            CudaInterop::DestroyCudaEvent(m_CudaEventStop);
#endif
    }

    void FluidSystemGPU::Init()
    {
        if (m_Initialized)
            return;

        ENGINE_INFO("[Fluid] Init() called, particleCount={}", m_ParticleCount);

        // Fluid-specific shaders
        m_EmitShader = Shader::Create("assets/shaders/fluid_emit.glsl");
        m_SimulateShader = Shader::Create("assets/shaders/fluid_simulate.glsl");

        // Reuse existing SPH shaders
        m_SPHDensityShader = Shader::Create("assets/shaders/sph_density.glsl");
        m_SPHForceShader = Shader::Create("assets/shaders/sph_force.glsl");

        // PCISPH shaders
        m_PCISPHInitShader = Shader::Create("assets/shaders/sph_pcisph_init.glsl");
        m_PCISPHPredictShader = Shader::Create("assets/shaders/sph_pcisph_predict.glsl");
        m_PCISPHDensityShader = Shader::Create("assets/shaders/sph_pcisph_density.glsl");
        m_PCISPHForceShader = Shader::Create("assets/shaders/sph_pcisph_force.glsl");
        m_PCISPHApplyShader = Shader::Create("assets/shaders/sph_pcisph_apply.glsl");

        // Particle buffer: zero-initialized, 80 bytes per particle
        uint32_t totalBytes = m_ParticleCount * sizeof(FluidGPUParticle);
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
            if (CudaGLInteropContext::ProbeDeviceMatch())
            {
                m_CudaInterop = CreateScope<CudaGLInteropContext>();
                m_CudaSlotParticle =
                    m_CudaInterop->RegisterBuffer(m_ParticleBuffer->GetRendererID(), "FluidParticleBuffer");
                if (m_CudaSlotParticle >= 0)
                {
                    m_CudaSPHCtx = CudaInterop::CreateSPHContext(m_ParticleCount, 64, MAX_RIGID_BODIES);
                    m_CudaEventStart = CudaInterop::CreateCudaEvent();
                    m_CudaEventStop = CudaInterop::CreateCudaEvent();
                    m_UseCudaPath = true;
                    ENGINE_INFO("[Fluid] CUDA SPH sidecar activated.");
                }
                else
                {
                    ENGINE_WARN("[Fluid] CUDA RegisterBuffer failed (slot=-1), staying on GL path.");
                }
            }
            else
            {
                ENGINE_WARN("[Fluid] CUDA ProbeDeviceMatch failed, staying on GL path.");
            }
        }
#endif
    }

    void FluidSystemGPU::InitSPH(float smoothingRadius)
    {
        if (m_SPHInitialized)
            return;

        float cellSize = 2.0f * smoothingRadius;
        uint32_t gridSize = 64;
        m_Grid.Init(m_ParticleCount, gridSize, cellSize);
        m_SPHInitialized = true;
    }

    void FluidSystemGPU::InitPCISPH()
    {
        if (m_PCISPHInitialized)
            return;
        m_PCISPHBuffer = ShaderStorageBuffer::CreateGPUOnly(m_ParticleCount * 48, 1);
        m_PCISPHInitialized = true;
    }

    void FluidSystemGPU::InitRigidBodyBuffer()
    {
        if (m_RigidBodyBuffer)
            return;
        m_RigidBodyBuffer = ShaderStorageBuffer::Create(MAX_RIGID_BODIES * sizeof(FluidGPURigidBody), 3);
    }

    uint32_t FluidSystemGPU::UploadRigidBodies(entt::registry* registry)
    {
        if (!registry || !m_RigidBodyBuffer)
            return 0;

        std::vector<FluidGPURigidBody> bodies;
        bodies.reserve(MAX_RIGID_BODIES);

        // 收集所有具有碰撞器的实体（不要求有 RigidBodyComponent）
        auto boxView = registry->view<TransformComponent, BoxColliderComponent>();
        for (auto entity : boxView)
        {
            if (bodies.size() >= MAX_RIGID_BODIES)
                break;

            auto& tc = boxView.get<TransformComponent>(entity);
            auto& bc = boxView.get<BoxColliderComponent>(entity);
            glm::mat4 rotMat = glm::toMat4(glm::quat(tc.Rotation));

            FluidGPURigidBody body{};
            body.posAndType = glm::vec4(tc.Translation + bc.Offset, 0.0f);
            body.rotCol0 = glm::vec4(rotMat[0][0], rotMat[0][1], rotMat[0][2], 0.0f);
            body.rotCol1 = glm::vec4(rotMat[1][0], rotMat[1][1], rotMat[1][2], 0.0f);
            body.rotCol2 = glm::vec4(rotMat[2][0], rotMat[2][1], rotMat[2][2], 0.0f);
            body.halfExtents = glm::vec4(bc.HalfExtents * tc.Scale, 0.0f);
            body.linearVel = glm::vec4(0.0f);
            body.angularVel = glm::vec4(0.0f);
            bodies.push_back(body);
        }

        auto sphereView = registry->view<TransformComponent, SphereColliderComponent>();
        for (auto entity : sphereView)
        {
            if (bodies.size() >= MAX_RIGID_BODIES)
                break;

            auto& tc = sphereView.get<TransformComponent>(entity);
            auto& sc = sphereView.get<SphereColliderComponent>(entity);
            glm::mat4 rotMat = glm::toMat4(glm::quat(tc.Rotation));

            FluidGPURigidBody body{};
            body.posAndType = glm::vec4(tc.Translation + sc.Offset, 1.0f);
            body.rotCol0 = glm::vec4(rotMat[0][0], rotMat[0][1], rotMat[0][2], 0.0f);
            body.rotCol1 = glm::vec4(rotMat[1][0], rotMat[1][1], rotMat[1][2], 0.0f);
            body.rotCol2 = glm::vec4(rotMat[2][0], rotMat[2][1], rotMat[2][2], 0.0f);
            float maxScale = std::max({tc.Scale.x, tc.Scale.y, tc.Scale.z});
            body.halfExtents = glm::vec4(sc.Radius * maxScale, 0.0f, 0.0f, 0.0f);
            body.linearVel = glm::vec4(0.0f);
            body.angularVel = glm::vec4(0.0f);
            bodies.push_back(body);
        }

        if (!bodies.empty())
            m_RigidBodyBuffer->SetData(bodies.data(), static_cast<uint32_t>(bodies.size() * sizeof(FluidGPURigidBody)));

        return static_cast<uint32_t>(bodies.size());
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

    void FluidSystemGPU::Update(float dt, const glm::vec3& emitterPos, const FluidEmitterComponent& emitter,
                                entt::registry* registry)
    {
        if (!m_Initialized)
            return;

        float clampedDt = std::min(dt, 0.05f);
        m_TotalTime += dt;

        // Lazy init SPH grid
        if (!m_SPHInitialized)
            InitSPH(emitter.SmoothingRadius);

        // CPU-side kernel constant precomputation
        float h = emitter.SmoothingRadius;
        float h2 = h * h;
        float h6 = h2 * h2 * h2;
        float h9 = h6 * h2 * h;
        float poly6Coeff = 315.0f / (64.0f * glm::pi<float>() * h9);
        float spikyCoeff = -45.0f / (glm::pi<float>() * h6);

#ifdef ENGINE_ENABLE_CUDA
        bool cudaSucceeded = false;
        if (m_UseCudaPath)
        {
            if (m_CudaInterop->MapAll())
            {
                void* stream = m_CudaInterop->GetStream();
                void* devParticles = m_CudaInterop->GetMappedPointer(m_CudaSlotParticle);
                CudaInterop::RecordCudaEvent(m_CudaEventStart, stream);

                float cellSize = 2.0f * h;

                // Grid Build（内部用 CUB prefix sum，替代 3 dispatch）
                CudaInterop::LaunchSPHGridBuild(m_CudaSPHCtx, devParticles, m_ParticleCount, 64, cellSize, stream);

                // 构造 SPHParams
                CudaInterop::SPHParams p{};
                p.smoothingRadius = h;
                p.poly6Coeff = poly6Coeff;
                p.spikyCoeff = spikyCoeff;
                p.particleMass = emitter.ParticleMass;
                p.restDensity = emitter.RestDensity;
                p.gasConstant = emitter.GasConstant;
                p.viscosity = emitter.Viscosity;
                p.surfaceTension = emitter.SurfaceTension;
                p.deltaTime = clampedDt;
                p.gridSize = 64;
                p.cellSize = cellSize;
                p.aliveCount = static_cast<int>(m_ParticleCount);
                p.gravity[0] = emitter.Gravity.x;
                p.gravity[1] = emitter.Gravity.y;
                p.gravity[2] = emitter.Gravity.z;
                p.warmupTime = 0.0f; // 流体粒子永久存活，不需要 warmup

                // 刚体数据：CPU→CUDA 每帧上传（WCSPH/PCISPH 共用）
                uint32_t cudaRigidBodyCount = 0;
                if (emitter.RigidBodyCoupling && registry)
                {
                    std::vector<FluidGPURigidBody> bodies;
                    bodies.reserve(MAX_RIGID_BODIES);
                    auto boxView = registry->view<TransformComponent, BoxColliderComponent>();
                    for (auto entity : boxView)
                    {
                        if (bodies.size() >= MAX_RIGID_BODIES)
                            break;
                        auto& tc = boxView.get<TransformComponent>(entity);
                        auto& bc = boxView.get<BoxColliderComponent>(entity);
                        glm::mat4 rotMat = glm::toMat4(glm::quat(tc.Rotation));
                        FluidGPURigidBody body{};
                        body.posAndType = glm::vec4(tc.Translation + bc.Offset, 0.0f);
                        body.rotCol0 = glm::vec4(rotMat[0][0], rotMat[0][1], rotMat[0][2], 0.0f);
                        body.rotCol1 = glm::vec4(rotMat[1][0], rotMat[1][1], rotMat[1][2], 0.0f);
                        body.rotCol2 = glm::vec4(rotMat[2][0], rotMat[2][1], rotMat[2][2], 0.0f);
                        body.halfExtents = glm::vec4(bc.HalfExtents * tc.Scale, 0.0f);
                        body.linearVel = glm::vec4(0.0f);
                        body.angularVel = glm::vec4(0.0f);
                        bodies.push_back(body);
                    }
                    auto sphereView = registry->view<TransformComponent, SphereColliderComponent>();
                    for (auto entity : sphereView)
                    {
                        if (bodies.size() >= MAX_RIGID_BODIES)
                            break;
                        auto& tc = sphereView.get<TransformComponent>(entity);
                        auto& sc = sphereView.get<SphereColliderComponent>(entity);
                        glm::mat4 rotMat = glm::toMat4(glm::quat(tc.Rotation));
                        FluidGPURigidBody body{};
                        body.posAndType = glm::vec4(tc.Translation + sc.Offset, 1.0f);
                        body.rotCol0 = glm::vec4(rotMat[0][0], rotMat[0][1], rotMat[0][2], 0.0f);
                        body.rotCol1 = glm::vec4(rotMat[1][0], rotMat[1][1], rotMat[1][2], 0.0f);
                        body.rotCol2 = glm::vec4(rotMat[2][0], rotMat[2][1], rotMat[2][2], 0.0f);
                        float maxScale = std::max({tc.Scale.x, tc.Scale.y, tc.Scale.z});
                        body.halfExtents = glm::vec4(sc.Radius * maxScale, 0.0f, 0.0f, 0.0f);
                        body.linearVel = glm::vec4(0.0f);
                        body.angularVel = glm::vec4(0.0f);
                        bodies.push_back(body);
                    }
                    cudaRigidBodyCount = static_cast<uint32_t>(bodies.size());
                    if (!bodies.empty())
                        CudaInterop::SPHUploadRigidBodies(m_CudaSPHCtx, bodies.data(), cudaRigidBodyCount);
                }

                // SPH Density
                CudaInterop::LaunchSPHDensity(m_CudaSPHCtx, devParticles, p, stream);

                CudaInterop::PCISPHIterParams ip{};
                ip.pcisphDelta = emitter.PCISPHDelta;
                ip.boundaryStiffness = emitter.BoundaryStiffness;
                ip.boundaryDamping = emitter.BoundaryDamping;
                ip.rigidBodyCount = static_cast<int>(cudaRigidBodyCount);

                if (emitter.PCISPHEnabled)
                {
                    CudaInterop::LaunchPCISPHInit(m_CudaSPHCtx, devParticles, p, stream);

                    int iterations = std::clamp(emitter.PCISPHIterations, 1, 8);
                    for (int iter = 0; iter < iterations; ++iter)
                    {
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
                sp.deltaTime = clampedDt;
                sp.damping = emitter.Damping;
                sp.gravity[0] = emitter.PCISPHEnabled ? 0.0f : emitter.Gravity.x;
                sp.gravity[1] = emitter.PCISPHEnabled ? 0.0f : emitter.Gravity.y;
                sp.gravity[2] = emitter.PCISPHEnabled ? 0.0f : emitter.Gravity.z;
                sp.boundaryMin[0] = emitter.BoundaryMin.x;
                sp.boundaryMin[1] = emitter.BoundaryMin.y;
                sp.boundaryMin[2] = emitter.BoundaryMin.z;
                sp.boundaryMax[0] = emitter.BoundaryMax.x;
                sp.boundaryMax[1] = emitter.BoundaryMax.y;
                sp.boundaryMax[2] = emitter.BoundaryMax.z;
                sp.useBoundary = emitter.UseBoundary ? 1 : 0;
                sp.particleCount = static_cast<int>(m_ParticleCount);
                CudaInterop::LaunchSPHSimulate(devParticles, sp, stream);

                CudaInterop::RecordCudaEvent(m_CudaEventStop, stream);
                m_CudaInterop->UnmapAll();

                float ms = CudaInterop::CudaEventElapsedMs(m_CudaEventStart, m_CudaEventStop);
                ENGINE_INFO("[Fluid] CUDA SPH frame: {:.2f} ms", ms);
                cudaSucceeded = true;
            }
            else
            {
                ENGINE_WARN("[Fluid] CUDA MapAll failed; falling back to GL.");
                m_UseCudaPath = false;
            }
        }
        if (!cudaSucceeded)
#endif
        { // ---- GL Compute 路径（原有代码）----

            // Bind particle + alive list
            m_ParticleBuffer->Bind(0);
            m_AliveList->Bind(2);

            // --- SPH Pipeline ---
            float cellSize = m_Grid.GetCellSize();
            int gridSize = static_cast<int>(m_Grid.GetGridSize());

            // Build spatial hash grid
            m_Grid.Build(m_ParticleCount);

            // SPH Density
            m_SPHDensityShader->Bind();
            m_SPHDensityShader->SetInt("u_AliveCount", static_cast<int>(m_ParticleCount));
            m_SPHDensityShader->SetFloat("u_SmoothingRadius", h);
            m_SPHDensityShader->SetFloat("u_ParticleMass", emitter.ParticleMass);
            m_SPHDensityShader->SetFloat("u_RestDensity", emitter.RestDensity);
            m_SPHDensityShader->SetFloat("u_GasConstant", emitter.GasConstant);
            m_SPHDensityShader->SetInt("u_GridSize", gridSize);
            m_SPHDensityShader->SetFloat("u_CellSize", cellSize);
            m_SPHDensityShader->SetFloat("u_Poly6Coeff", poly6Coeff);

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
                    rigidBodyCount = UploadRigidBodies(registry);
                }

                m_PCISPHBuffer->Bind(1);
                if (m_RigidBodyBuffer)
                    m_RigidBodyBuffer->Bind(3);

                // PCISPH Init
                m_PCISPHInitShader->Bind();
                m_PCISPHInitShader->SetInt("u_AliveCount", static_cast<int>(m_ParticleCount));
                m_PCISPHInitShader->SetFloat("u_SmoothingRadius", h);
                m_PCISPHInitShader->SetFloat("u_ParticleMass", emitter.ParticleMass);
                m_PCISPHInitShader->SetFloat("u_Viscosity", emitter.Viscosity);
                m_PCISPHInitShader->SetFloat("u_DeltaTime", clampedDt);
                m_PCISPHInitShader->SetInt("u_GridSize", gridSize);
                m_PCISPHInitShader->SetFloat("u_CellSize", cellSize);
                m_PCISPHInitShader->SetFloat3("u_Gravity", emitter.Gravity);
                m_PCISPHInitShader->SetFloat("u_SurfaceTension", emitter.SurfaceTension);
                m_PCISPHInitShader->SetFloat("u_SpikyCoeff", spikyCoeff);
                RenderCommand::DispatchCompute(sphGroups);
                RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                int iterations = std::clamp(emitter.PCISPHIterations, 1, 8);

                // 单帧内完成所有 PCISPH 迭代（Predict → Density → Force）
                for (int iter = 0; iter < iterations; iter++)
                {
                    // Predict: x* = pos + dt * v*
                    m_PCISPHPredictShader->Bind();
                    m_PCISPHPredictShader->SetInt("u_AliveCount", static_cast<int>(m_ParticleCount));
                    m_PCISPHPredictShader->SetFloat("u_DeltaTime", clampedDt);
                    RenderCommand::DispatchCompute(sphGroups);
                    RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                    // Density: 在预测位置上计算密度和压力
                    m_PCISPHDensityShader->Bind();
                    m_PCISPHDensityShader->SetInt("u_AliveCount", static_cast<int>(m_ParticleCount));
                    m_PCISPHDensityShader->SetFloat("u_SmoothingRadius", h);
                    m_PCISPHDensityShader->SetFloat("u_ParticleMass", emitter.ParticleMass);
                    m_PCISPHDensityShader->SetFloat("u_RestDensity", emitter.RestDensity);
                    m_PCISPHDensityShader->SetFloat("u_PCISPHDelta", emitter.PCISPHDelta);
                    m_PCISPHDensityShader->SetInt("u_GridSize", gridSize);
                    m_PCISPHDensityShader->SetFloat("u_CellSize", cellSize);
                    m_PCISPHDensityShader->SetFloat("u_Poly6Coeff", poly6Coeff);
                    RenderCommand::DispatchCompute(sphGroups);
                    RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                    // Force: 压力梯度力 + 刚体边界力 → v* += a_pressure * dt
                    m_PCISPHForceShader->Bind();
                    m_PCISPHForceShader->SetInt("u_AliveCount", static_cast<int>(m_ParticleCount));
                    m_PCISPHForceShader->SetFloat("u_SmoothingRadius", h);
                    m_PCISPHForceShader->SetFloat("u_ParticleMass", emitter.ParticleMass);
                    m_PCISPHForceShader->SetFloat("u_DeltaTime", clampedDt);
                    m_PCISPHForceShader->SetInt("u_GridSize", gridSize);
                    m_PCISPHForceShader->SetFloat("u_CellSize", cellSize);
                    m_PCISPHForceShader->SetInt("u_RigidBodyCount", static_cast<int>(rigidBodyCount));
                    m_PCISPHForceShader->SetFloat("u_BoundaryStiffness", emitter.BoundaryStiffness);
                    m_PCISPHForceShader->SetFloat("u_BoundaryDamping", emitter.BoundaryDamping);
                    m_PCISPHForceShader->SetFloat("u_SpikyCoeff", spikyCoeff);
                    RenderCommand::DispatchCompute(sphGroups);
                    RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);
                }

                // Apply: 将最终预测速度写回粒子（每帧都执行）
                m_PCISPHApplyShader->Bind();
                m_PCISPHApplyShader->SetInt("u_AliveCount", static_cast<int>(m_ParticleCount));
                RenderCommand::DispatchCompute(sphGroups);
                RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);
            }
            else
            {
                // --- WCSPH path ---
                m_SPHForceShader->Bind();
                m_SPHForceShader->SetInt("u_AliveCount", static_cast<int>(m_ParticleCount));
                m_SPHForceShader->SetFloat("u_SmoothingRadius", h);
                m_SPHForceShader->SetFloat("u_ParticleMass", emitter.ParticleMass);
                m_SPHForceShader->SetFloat("u_Viscosity", emitter.Viscosity);
                m_SPHForceShader->SetFloat("u_DeltaTime", clampedDt);
                m_SPHForceShader->SetInt("u_GridSize", gridSize);
                m_SPHForceShader->SetFloat("u_CellSize", cellSize);
                m_SPHForceShader->SetFloat("u_SurfaceTension", emitter.SurfaceTension);
                m_SPHForceShader->SetFloat("u_SpikyCoeff", spikyCoeff);

                uint32_t rigidBodyCount = 0;
                if (emitter.RigidBodyCoupling && registry)
                {
                    InitRigidBodyBuffer();
                    rigidBodyCount = UploadRigidBodies(registry);
                    m_RigidBodyBuffer->Bind(3);
                }
                m_SPHForceShader->SetInt("u_RigidBodyCount", static_cast<int>(rigidBodyCount));
                m_SPHForceShader->SetFloat("u_BoundaryStiffness", emitter.BoundaryStiffness);
                m_SPHForceShader->SetFloat("u_BoundaryDamping", emitter.BoundaryDamping);

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
            m_SimulateShader->SetFloat3("u_BoundaryMin", emitter.BoundaryMin);
            m_SimulateShader->SetFloat3("u_BoundaryMax", emitter.BoundaryMax);
            m_SimulateShader->SetInt("u_UseBoundary", emitter.UseBoundary ? 1 : 0);

            uint32_t simGroups = (m_ParticleCount + 255) / 256;
            RenderCommand::DispatchCompute(simGroups);
            RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

        } // end if (!cudaSucceeded)
    }

} // namespace Engine
