#pragma once

#include "Core/Base.h"
#include "Renderer/Shader.h"
#include "Renderer/SPHCommon.h"
#include "Renderer/SpatialHashGrid.h"
#include "Renderer/GPUAsyncReadback.h"
#include "Renderer/StorageBuffer.h"
#include "Renderer/UniformBuffer.h"
#include "Renderer/VertexArray.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace Engine
{

    struct ParticleEmitterComponent;

    class ParticleSystemGPU
    {
    public:
        enum class ABConfigSource : uint8_t
        {
            Default = 0,
            UI,
            Env
        };

        struct ABConfigSnapshot
        {
            bool           ForceGL                    = false;
            bool           DisableCounterReadback     = false;
            bool           ForceGLLockedByEnv         = false;
            bool           DisableReadbackLockedByEnv = false;
            ABConfigSource ForceGLSource              = ABConfigSource::Default;
            ABConfigSource DisableReadbackSource      = ABConfigSource::Default;
        };

        ParticleSystemGPU(uint32_t maxParticles);
        ~ParticleSystemGPU();

        static ABConfigSnapshot GetABConfigSnapshot();
        static void             SetABConfigFromUI(bool forceGL, bool disableCounterReadback);
        static const char*      ABConfigSourceLabel(ABConfigSource source);

        // CUDA-GL 互操作是当前唯一启用路径。Vulkan-CUDA 互操作与 VulkanExternal smoke
        // kernel 已随 commit a18fd0c（2026-04-01）整体移除——若未来重做再加变体。
        enum class InteropBackend : uint8_t
        {
            CudaGL = 0
        };

        static InteropBackend GetRequestedInteropBackend();
        static const char*    InteropBackendLabel(InteropBackend backend);

        void Init();
        void Update(float                           dt,
                    const glm::vec3&                emitterPos,
                    const ParticleEmitterComponent& emitter,
                    entt::registry*                 registry = nullptr);
        void Render(const glm::mat4& viewMatrix, const glm::mat4& projection);

        uint32_t GetMaxParticles() const { return m_MaxParticles; }

    private:
        uint32_t m_MaxParticles;

        // GPU buffers
        Ref<ShaderStorageBuffer> m_ParticleBuffer; // binding 0
        Ref<ShaderStorageBuffer> m_DeadList;       // binding 1
        Ref<ShaderStorageBuffer> m_AliveList;      // binding 2
        Ref<ShaderStorageBuffer> m_CounterBuffer;  // binding 3
        Ref<ShaderStorageBuffer> m_IndirectArgs;   // binding 4

        // Shaders
        Ref<Shader> m_EmitShader;
        Ref<Shader> m_SimulateShader;
        Ref<Shader> m_CompactShader;
        Ref<Shader> m_RenderArgsShader;
        Ref<Shader> m_BillboardShader;

        // SPH shaders (共享结构体)
        SPHShaderSet m_SPHShaders;

        // Spatial hash grid for SPH
        SpatialHashGrid m_Grid;

        // Empty VAO for indirect draw
        Ref<VertexArray> m_EmptyVAO;

        bool m_Initialized    = false;
        bool m_SPHInitialized = false;

        // PCISPH
        Ref<ShaderStorageBuffer> m_PCISPHBuffer;    // binding 1 during SPH, 48B/particle
        Ref<ShaderStorageBuffer> m_RigidBodyBuffer; // binding 3 during SPH, 112B × MAX_RIGID_BODIES
        bool                     m_PCISPHInitialized = false;

        void InitPCISPH();
        void InitRigidBodyBuffer();

        float m_EmitAccumulator = 0.0f;
        float m_TotalTime       = 0.0f;

        // VMware/Mesa compatibility fallback:
        // use direct instanced draw instead of DrawArraysIndirect.
        bool     m_UseIndirectDraw         = true;
        bool     m_VMwareCompatMode        = false;
        bool     m_DisableSPHOnDriver      = false;
        bool     m_SPHDisableLogged        = false;
        uint32_t m_AliveCountForDirectDraw = 0;

        // 上一帧的活跃粒子数（用于 SPH dispatch）
        uint32_t m_LastAliveCount = 0;

        // ---- 跨后端等价性冒烟自检（ENGINE_PARTICLE_EQUIV_SMOKE=1 启用）----
        // 同一确定性初始态分别强制 GL / CUDA 各跑 FramesPerPass 帧后对比全池
        // 最大位置/速度偏差，用于捕获 alive-list 误索引、边界力语义等结构性
        // 分歧。原子散射顺序导致逐位一致不可达，故按容差判定。仅调试用途。
        struct EquivalenceSmoke
        {
            bool                 Enabled       = false;
            bool                 Initialized   = false;
            int                  Phase         = 0; // 0=GL 采集, 1=CUDA 采集, 2=完成/跳过
            int                  FrameIndex    = 0;
            int                  FramesPerPass = 60;
            float                FixedDt       = 1.0f / 60.0f;
            uint32_t             MaxParticles  = 0;
            std::vector<uint8_t> InitialState; // 全池 GPUParticleData 原始字节
            std::vector<uint8_t> FinalGl;
            std::vector<uint8_t> FinalCuda;
        };
        EquivalenceSmoke m_EquivSmoke;
        void             TickEquivalenceSmoke(bool& forceGL, float& dt, bool& suppressEmit);
        void             CaptureEquivalenceFrame();

        // 异步回读（避免同步阻塞）
        Ref<GPUAsyncReadback> m_Readback;

        // CUDA compute sidecar（Pimpl 模式隐藏 CUDA 依赖）
        struct CudaImpl;
        Scope<CudaImpl> m_CudaImpl;

        void InitSPH(float smoothingRadius);

        // ---- Vulkan path (非 SPH) ----
        // Vulkan 资源以 Pimpl 隐藏在 .cpp，避免 .h 引入 Vulkan 头依赖。
        struct VulkanResources;
        Scope<VulkanResources> m_VulkanResources;

        // Emit / Simulate 用 UBO 上传大块 emitter 参数
        Ref<UniformBuffer> m_EmitParamsUBO;
        Ref<UniformBuffer> m_SimParamsUBO;

        // SPH 共享 UBO（binding=12），density + force 共用同一份布局
        Ref<UniformBuffer> m_SPHParamsUBO;

        // Vulkan path Update 分派
        void UpdateVulkan(float                           dt,
                          const glm::vec3&                emitterPos,
                          const ParticleEmitterComponent& emitter,
                          entt::registry*                 registry);
        bool InitVulkanComputeResources();
        bool InitSPHVulkanPipelines();
        void DestroyVulkanComputeResources();
    };

} // namespace Engine
