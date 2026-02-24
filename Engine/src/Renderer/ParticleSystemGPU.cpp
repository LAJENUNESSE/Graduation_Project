#include "engpch.h"
#include "Renderer/ParticleSystemGPU.h"
#include "Scene/Components.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/RendererAPI.h"

#include <cmath>

namespace Engine
{

    // Must match GLSL struct layout: 5 x vec4 = 80 bytes
    struct GPUParticleData
    {
        glm::vec4 posAndLife;
        glm::vec4 velAndMaxLife;
        glm::vec4 startColor;
        glm::vec4 endColor;
        glm::vec4 params;       // x=sizeStart, y=sizeEnd, z=density(SPH), w=pressure(SPH)
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
        : m_MaxParticles(maxParticles)
    {
    }

    void ParticleSystemGPU::Init()
    {
        if (m_Initialized) return;

        // Load compute shaders
        m_EmitShader       = Shader::Create("assets/shaders/particle_emit.glsl");
        m_SimulateShader   = Shader::Create("assets/shaders/particle_simulate.glsl");
        m_RenderArgsShader = Shader::Create("assets/shaders/particle_render_args.glsl");
        m_BillboardShader  = Shader::Create("assets/shaders/particle_billboard.glsl");

        // SPH shaders (loaded eagerly, only dispatched when SPHEnabled)
        m_SPHDensityShader = Shader::Create("assets/shaders/sph_density.glsl");
        m_SPHForceShader   = Shader::Create("assets/shaders/sph_force.glsl");

        // Allocate particle pool — MUST be zero-initialized so all particles
        // start with life=0.0 (properly dead). Undefined buffer data may contain
        // NaN or positive life values, causing simulate to treat uninitialized
        // particles as alive and permanently corrupting the dead/alive counters.
        uint32_t particleSize = sizeof(GPUParticleData); // 80 bytes
        uint32_t totalBytes = m_MaxParticles * particleSize;
        std::vector<uint8_t> zeroData(totalBytes, 0);
        m_ParticleBuffer = ShaderStorageBuffer::Create(zeroData.data(), totalBytes, 0);

        // Fill dead list with indices [0, 1, 2, ..., MAX-1]
        std::vector<uint32_t> deadIndices(m_MaxParticles);
        for (uint32_t i = 0; i < m_MaxParticles; i++)
            deadIndices[i] = i;
        m_DeadList = ShaderStorageBuffer::Create(deadIndices.data(),
                                                  m_MaxParticles * sizeof(uint32_t), 1);

        // Alive list (empty at start)
        m_AliveList = ShaderStorageBuffer::Create(m_MaxParticles * sizeof(uint32_t), 2);

        // Counter buffer: deadCount=MAX, aliveCount=0, emitCount=0, pad=0
        CounterData counters{m_MaxParticles, 0, 0, 0};
        m_CounterBuffer = ShaderStorageBuffer::Create(&counters, sizeof(CounterData), 3);

        // Indirect draw args: count=6, instanceCount=0, first=0, baseInstance=0
        IndirectDrawCommand cmd{6, 0, 0, 0};
        m_IndirectArgs = ShaderStorageBuffer::Create(&cmd, sizeof(IndirectDrawCommand), 4);

        // Empty VAO for billboard rendering
        m_EmptyVAO = VertexArray::Create();

        m_Initialized = true;
    }

    void ParticleSystemGPU::InitSPH(float smoothingRadius)
    {
        if (m_SPHInitialized) return;

        // Grid cell size = 2 * smoothing radius (保证邻域在 3x3x3 cell 内)
        float cellSize = 2.0f * smoothingRadius;
        uint32_t gridSize = 64;

        m_Grid.Init(m_MaxParticles, gridSize, cellSize);
        m_SPHInitialized = true;
    }

    void ParticleSystemGPU::Update(float dt, const glm::vec3& emitterPos,
                                    const ParticleEmitterComponent& emitter)
    {
        if (!m_Initialized) return;

        // ---- CPU-side: reset aliveCount, set emitCount ----
        uint32_t zero = 0;
        m_CounterBuffer->SetData(&zero, sizeof(uint32_t), 4);  // aliveCount = 0

        // Compute how many particles to emit (clamp dt to prevent first-frame spike)
        float clampedDt = std::min(dt, 0.05f);
        m_EmitAccumulator += emitter.EmitRate * clampedDt;
        uint32_t emitCount = static_cast<uint32_t>(m_EmitAccumulator);
        m_EmitAccumulator -= static_cast<float>(emitCount);

        // Add burst (user-set + collision-triggered)
        int totalBurst = emitter.BurstCount + emitter.CollisionBurstCount;
        if (totalBurst > 0)
            emitCount += static_cast<uint32_t>(totalBurst);

        // Clamp to MaxParticles — prevent shader atomic underflow
        emitCount = std::min(emitCount, m_MaxParticles);

        m_CounterBuffer->SetData(&emitCount, sizeof(uint32_t), 8);  // emitCount

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

            // Time-based seed for RNG
            static float totalTime = 0.0f;
            totalTime += dt;
            m_EmitShader->SetFloat("u_Time", totalTime);

            uint32_t groups = (emitCount + 63) / 64;
            RenderCommand::DispatchCompute(groups);
            RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);
        }

        // ---- SPH passes (only when SPHEnabled) ----
        if (emitter.SPHEnabled)
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

                uint32_t sphGroups = (m_LastAliveCount + 255) / 256;
                RenderCommand::DispatchCompute(sphGroups);
                RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

                // Pass 2c: SPH Force → directly updates velocity
                m_SPHForceShader->Bind();
                m_SPHForceShader->SetInt("u_AliveCount", static_cast<int>(m_LastAliveCount));
                m_SPHForceShader->SetFloat("u_SmoothingRadius", emitter.SPH_SmoothingRadius);
                m_SPHForceShader->SetFloat("u_ParticleMass", emitter.SPH_ParticleMass);
                m_SPHForceShader->SetFloat("u_Viscosity", emitter.SPH_Viscosity);
                m_SPHForceShader->SetFloat("u_DeltaTime", clampedDt);
                m_SPHForceShader->SetInt("u_GridSize", gridSize);
                m_SPHForceShader->SetFloat("u_CellSize", cellSize);

                RenderCommand::DispatchCompute(sphGroups);
                RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);
            }

            // Rebind DeadList(1) and IndirectArgs(4) — Grid passes temporarily
            // used these binding slots for CellHash and BlockSums.
            m_DeadList->Bind(1);
            m_IndirectArgs->Bind(4);
        }

        // ---- Pass 3: Simulate (gravity + damping + alive/dead management) ----
        // Clamp dt for simulate too: if the application stalls (save dialog, etc.),
        // a huge dt would kill ALL particles in one frame, corrupting counters.
        float simulateDt = std::min(dt, 0.05f);
        m_SimulateShader->Bind();
        m_SimulateShader->SetFloat("u_DeltaTime", simulateDt);
        m_SimulateShader->SetFloat3("u_Gravity", emitter.Gravity);
        m_SimulateShader->SetFloat("u_Damping", emitter.Damping);
        m_SimulateShader->SetInt("u_MaxParticles", static_cast<int>(m_MaxParticles));

        uint32_t simGroups = (m_MaxParticles + 255) / 256;
        RenderCommand::DispatchCompute(simGroups);
        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

        // ---- Pass 4: Render Args ----
        m_RenderArgsShader->Bind();
        RenderCommand::DispatchCompute(1);
        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage | BarrierBit::Command);

        // Read back aliveCount for next frame's SPH dispatch
        if (emitter.SPHEnabled)
        {
            CounterData counters{};
            m_CounterBuffer->GetData(&counters, sizeof(CounterData), 0);
            m_LastAliveCount = counters.aliveCount;
        }
    }

    void ParticleSystemGPU::Render(const glm::mat4& viewMatrix, const glm::mat4& projection)
    {
        if (!m_Initialized) return;

        // Bind buffers for vertex shader access
        m_ParticleBuffer->Bind(0);
        m_AliveList->Bind(2);

        m_BillboardShader->Bind();
        m_BillboardShader->SetMat4("u_View", viewMatrix);
        m_BillboardShader->SetMat4("u_Projection", projection);

        // Disable depth write, keep depth test
        RenderCommand::SetDepthMask(false);

        m_EmptyVAO->Bind();
        RenderCommand::DrawArraysIndirect(m_IndirectArgs->GetRendererID());

        // Restore depth write
        RenderCommand::SetDepthMask(true);
    }

} // namespace Engine
