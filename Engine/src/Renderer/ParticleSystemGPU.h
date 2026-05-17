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

        enum class InteropBackend : uint8_t
        {
            CudaGL         = 0,
            VulkanExternal = 1, // Deprecated: use CudaVulkan instead
            CudaVulkan     = 2  // CUDA-Vulkan external memory + timeline semaphore interop
        };

        static InteropBackend GetRequestedInteropBackend();
        static const char*    InteropBackendLabel(InteropBackend backend);

        /// 查询 VkExtSkeleton 运行时是否就绪（能力探测 + 自检通过）
        static bool IsVkExtSkeletonReady();

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
