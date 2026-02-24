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
        glm::vec4 params;
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

        // Allocate particle pool (all zeroed = all dead with life <= 0)
        uint32_t particleSize = sizeof(GPUParticleData); // 80 bytes
        m_ParticleBuffer = ShaderStorageBuffer::Create(m_MaxParticles * particleSize, 0);

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

        // Add burst
        if (emitter.BurstCount > 0)
            emitCount += static_cast<uint32_t>(emitter.BurstCount);

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

        // ---- Pass 2: Simulate ----
        m_SimulateShader->Bind();
        m_SimulateShader->SetFloat("u_DeltaTime", dt);
        m_SimulateShader->SetFloat3("u_Gravity", emitter.Gravity);
        m_SimulateShader->SetFloat("u_Damping", emitter.Damping);
        m_SimulateShader->SetInt("u_MaxParticles", static_cast<int>(m_MaxParticles));

        uint32_t simGroups = (m_MaxParticles + 255) / 256;
        RenderCommand::DispatchCompute(simGroups);
        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

        // ---- Pass 3: Render Args ----
        m_RenderArgsShader->Bind();
        RenderCommand::DispatchCompute(1);
        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage | BarrierBit::Command);
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
