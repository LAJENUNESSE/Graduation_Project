#pragma once

#include "Asset/AssetHandle.h"
#include "Core/Base.h"
#include "Core/Timestep.h"
#include "Renderer/FluidRenderer.h"
#include "Renderer/FluidSystemGPU.h"
#include "Renderer/ParticleSystemGPU.h"
#include "Renderer/PostProcessing.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/SceneRenderInput.h"
#include "Renderer/Shader.h"
#include "Renderer/Texture.h"
#include "Scene/Systems/AudioSystem.h"
#include "Scene/Systems/GrassRenderSystem.h"
#include "Scene/Systems/LightSystem.h"
#include "Scene/Systems/ShadowSystem.h"
#include "Scene/Systems/SkyboxSystem.h"
#include "Scene/Systems/TerrainRenderSystem.h"
#include "Scene/Systems/VideoSystem.h"

#include <functional>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Engine
{

    class EditorCamera;

    struct RenderContext
    {
        const EditorCamera*     Camera         = nullptr;
        entt::registry*         Registry       = nullptr;
        const SceneEntityIndex* EntityIndex    = nullptr;
        WorldTransformCache*    TransformCache = nullptr;
        float                   DeltaTime      = 0.0f;

        // 供 FluidPass 使用
        uint32_t SceneColorTexID = 0;
        uint32_t SceneDepthTexID = 0;

        // 供 SSAO Pass 使用（由 EditorLayer 设置）
        uint32_t SSAODepthTexID = 0;
        uint32_t ViewportWidth  = 0;
        uint32_t ViewportHeight = 0;
    };

    struct RenderPassConfig
    {
        std::string                         Name;
        std::function<void(RenderContext&)> ExecuteFn;
        bool                                Enabled = true;
    };

    class SceneRenderer
    {
    public:
        struct MeshSDFFrameStats
        {
            uint32_t ActiveEmitters   = 0;
            uint32_t BodyCount        = 0;
            uint32_t VoxelCount       = 0;
            uint32_t EstimatedSamples = 0;
            uint32_t Resolution       = 0;
            float    Band             = 0.0f;
            float    BuildCpuMs       = 0.0f;
        };

        void Init(uint32_t viewportWidth = 1280, uint32_t viewportHeight = 720);
        void Shutdown();

        void BeginScene(const EditorCamera& camera, const SceneRenderInput& input);
        void Render();
        void EndScene();

        // 供 MSAA 重绘只走渲染（不重复收集光照/阴影）
        void RenderGeometryAndSkybox();
        // 供 MSAA 解析后单独绘制粒子，避免颜色被 blit 覆盖
        void RenderParticlePass();

        ShadowSystem&        GetShadowSystem() { return m_ShadowSystem; }
        SkyboxSystem&        GetSkyboxSystem() { return m_SkyboxSystem; }
        TerrainRenderSystem& GetTerrainSystem() { return m_TerrainSystem; }
        GrassRenderSystem&   GetGrassSystem() { return m_GrassSystem; }
        AudioSystem&         GetAudioSystem() { return m_AudioSystem; }
        VideoSystem&         GetVideoSystem() { return m_VideoSystem; }
        FluidRenderer&       GetFluidRenderer() { return m_FluidRenderer; }

        // 逐实体释放粒子/流体 GPU 缓存
        void                                                     ReleaseParticleSystem(uint32_t entityID);
        void                                                     ReleaseFluidSystem(uint32_t entityID);
        const std::unordered_map<uint32_t, Ref<FluidSystemGPU>>& GetFluidSystems() const { return m_FluidSystems; }
        const MeshSDFFrameStats& GetMeshSDFFrameStats() const { return m_MeshSDFFrameStats; }

        // SSAO 设置
        bool&  GetSSAOEnabled() { return m_SSAOEnabled; }
        float& GetSSAORadius() { return m_SSAORadius; }
        float& GetSSAOBias() { return m_SSAOBias; }
        int&   GetSSAOKernelSize() { return m_SSAOKernelSize; }
        float& GetSSAOIntensity() { return m_SSAOIntensity; }

        // IBL 调试模式
        int& GetIBLDebugMode() { return m_IBLDebugMode; }

        // 供 MSAA 解析后单独绘制流体，避免颜色被 blit 覆盖
        void RenderFluidPass();

        // Mesh SDF 调试可视化
        bool& GetShowMeshSDFBounds() { return m_ShowMeshSDFBounds; }
        bool& GetShowMeshSDFStats() { return m_ShowMeshSDFStats; }

        // 供 EditorLayer 精细控制 pass 执行
        std::vector<RenderPassConfig>& GetPassQueue() { return m_PassQueue; }
        RenderContext&                 GetContext() { return m_Context; }

        // 完整渲染管线：Shadow → HDR → MSAA → Fluid → DebugDraw → PostProcess → 输出到 targetFBO
        void SetHDRFramebuffer(const Ref<Framebuffer>& hdrFBO);
        void SetPostProcessing(PostProcessing* pp, PostProcessingSettings* settings);

        using DebugDrawCallback = std::function<void()>;
        void SetDebugDrawCallback(DebugDrawCallback callback);

        void RenderPipeline(const Ref<Framebuffer>& targetFBO);
        void RenderEditorPicking(const Ref<Framebuffer>& pickingFBO);
        void ResizeHDR(uint32_t width, uint32_t height);

    private:
        void UpdateParticleSystems();

        std::vector<RenderPassConfig> m_PassQueue;

        RenderContext    m_Context;
        LightEnvironment m_LightEnv;
        ShadowData       m_ShadowData;

        ShadowSystem        m_ShadowSystem;
        SkyboxSystem        m_SkyboxSystem;
        TerrainRenderSystem m_TerrainSystem;
        GrassRenderSystem   m_GrassSystem;
        RenderQueue         m_RenderQueue;
        AudioSystem         m_AudioSystem;
        VideoSystem         m_VideoSystem;

        Ref<Shader>    m_PBRShader;
        Ref<Texture2D> m_WhiteTexture;
        AssetHandle    m_WhiteTextureHandle;

        // SSAO 资源
        Ref<Shader>      m_SSAOShader;
        Ref<Shader>      m_SSAOBlurShader;
        Ref<Framebuffer> m_SSAOFBO;
        Ref<Framebuffer> m_SSAOBlurFBO;
        Ref<Texture2D>   m_SSAONoiseTex;
        uint32_t         m_SSAOBlurredTexID = 0;
        bool             m_SSAOEnabled      = false;
        float            m_SSAORadius       = 0.5f;
        float            m_SSAOBias         = 0.025f;
        int              m_SSAOKernelSize   = 32;
        float            m_SSAOIntensity    = 1.5f;
        int              m_IBLDebugMode     = 0; // 0=正常, 1=Irradiance, 2=Prefilter, 3=BRDF LUT, 4=法线
        Ref<VertexArray> m_FullscreenQuadVAO;

        // 追踪当前绑定的 registry，用于检测场景切换（不受 EndScene 清空影响）
        entt::registry* m_BoundRegistry = nullptr;

        // Particle systems keyed by entity ID
        std::unordered_map<uint32_t, Ref<ParticleSystemGPU>> m_ParticleSystems;

        // Fluid systems keyed by entity ID
        std::unordered_map<uint32_t, Ref<FluidSystemGPU>> m_FluidSystems;
        std::unordered_set<uint32_t>                      m_FluidEmitted; // 替代 FluidEmitterComponent::Emitted
        FluidRenderer                                     m_FluidRenderer;
        float                                             m_TotalTime         = 0.0f;
        bool                                              m_ShowMeshSDFBounds = false;
        bool                                              m_ShowMeshSDFStats  = true;
        MeshSDFFrameStats                                 m_MeshSDFFrameStats;

        // 完整渲染管线依赖（由外部注入）
        Ref<Framebuffer>        m_HDRFramebuffer;
        PostProcessing*         m_PostProcessing         = nullptr;
        PostProcessingSettings* m_PostProcessingSettings = nullptr;
        DebugDrawCallback       m_DebugDrawCallback;
    };

} // namespace Engine
