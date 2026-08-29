#include "engpch.h"
#include "Renderer/SceneRenderer.h"
#include "Asset/AssetManager.h"
#include "Core/Log.h"
#include "Debug/PerformanceMonitor.h"
#include "Debug/ProfileTimer.h"
#include "Renderer/Buffer.h"
#include "Renderer/EditorCamera.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Renderer.h"
#include "Renderer/RendererAPI.h"
#include "Renderer/RendererCapabilities.h"
#include "Scene/Components.h"
#include "Scene/Systems/MeshRenderSystem.h"
#include "Scene/WorldTransformService.h"

#include <random>

namespace Engine
{

    // 生成 SSAO 采样核
    static std::vector<glm::vec3> GenerateSSAOKernel(int kernelSize)
    {
        std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
        std::default_random_engine            generator(std::random_device{}());

        std::vector<glm::vec3> ssaoKernel;
        for (int i = 0; i < kernelSize; ++i)
        {
            glm::vec3 sample(randomFloats(generator) * 2.0f - 1.0f, randomFloats(generator) * 2.0f - 1.0f,
                             randomFloats(generator) // 半球：z >= 0
            );
            sample = glm::normalize(sample);
            sample *= randomFloats(generator);

            // 加速插值：让更多采样点靠近原点
            float scale = static_cast<float>(i) / static_cast<float>(kernelSize);
            scale       = 0.1f + scale * scale * 0.9f; // lerp(0.1, 1.0, scale^2)
            sample *= scale;

            ssaoKernel.push_back(sample);
        }
        return ssaoKernel;
    }

    void SceneRenderer::Init(uint32_t viewportWidth, uint32_t viewportHeight)
    {
        m_PBRShader = Shader::Create("assets/shaders/PBR.glsl");

        m_WhiteTextureHandle = AssetManager::Load<Texture2D>("builtin:white");
        m_WhiteTexture       = AssetManager::GetRef<Texture2D>(m_WhiteTextureHandle);

        m_ShadowSystem.Init();
        m_SkyboxSystem.Init();
        m_TerrainSystem.Init();
        m_GrassSystem.Init();
        m_AudioSystem.Init();
        m_VideoSystem.Init();

        // ---- SSAO 初始化 ----
        m_SSAOShader     = Shader::Create("assets/shaders/SSAO.glsl");
        m_SSAOBlurShader = Shader::Create("assets/shaders/SSAOBlur.glsl");

        // SSAO FBO（R16F 单通道，半分辨率）
        {
            uint32_t halfW = std::max(viewportWidth / 2, 1u);
            uint32_t halfH = std::max(viewportHeight / 2, 1u);

            FramebufferSpecification ssaoSpec;
            ssaoSpec.Attachments = {FramebufferTextureFormat::R16F};
            ssaoSpec.Width       = halfW;
            ssaoSpec.Height      = halfH;
            m_SSAOFBO            = Framebuffer::Create(ssaoSpec);

            FramebufferSpecification ssaoBlurSpec;
            ssaoBlurSpec.Attachments = {FramebufferTextureFormat::R16F};
            ssaoBlurSpec.Width       = halfW;
            ssaoBlurSpec.Height      = halfH;
            m_SSAOBlurFBO            = Framebuffer::Create(ssaoBlurSpec);
        }

        // SSAO 4x4 噪声纹理
        {
            std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
            std::default_random_engine            generator(std::random_device{}());
            std::vector<glm::vec3>                ssaoNoise;
            for (int i = 0; i < 16; i++)
            {
                glm::vec3 noise(randomFloats(generator) * 2.0f - 1.0f, randomFloats(generator) * 2.0f - 1.0f, 0.0f);
                ssaoNoise.push_back(noise);
            }

            TextureSpecification noiseSpec;
            noiseSpec.Format       = TextureFormat::RGBA16F;
            noiseSpec.MinFilter    = TextureFilter::Nearest;
            noiseSpec.MagFilter    = TextureFilter::Nearest;
            noiseSpec.WrapS        = TextureWrap::Repeat;
            noiseSpec.WrapT        = TextureWrap::Repeat;
            noiseSpec.UploadLayout = TextureDataLayout::RGB_Float;
            m_SSAONoiseTex         = Texture2D::Create(ssaoNoise.data(), 4, 4, noiseSpec);
        }

        // 全屏四边形 VAO（供 SSAO pass 使用）
        {
            float quadVertices[] = {
                -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f,
            };
            uint32_t quadIndices[] = {0, 1, 2, 2, 3, 0};

            m_FullscreenQuadVAO = VertexArray::Create();
            auto vb             = VertexBuffer::Create(quadVertices, sizeof(quadVertices));
            vb->SetLayout({
                {ShaderDataType::Float2, "a_Position"},
                {ShaderDataType::Float2, "a_TexCoord"},
            });
            m_FullscreenQuadVAO->AddVertexBuffer(vb);
            auto ib = IndexBuffer::Create(quadIndices, 6);
            m_FullscreenQuadVAO->SetIndexBuffer(ib);
        }

        m_PassQueue.push_back(
            {"LightCollect", [this](RenderContext& ctx)
             { m_LightEnv = LightSystem::CollectLights(*ctx.Registry, *ctx.EntityIndex, ctx.TransformCache); }});

        m_PassQueue.push_back(
            {"ShadowPass", [this](RenderContext& ctx)
             {
                 // 使用 CSM 版本
                 m_ShadowData = m_ShadowSystem.ExecuteCSM(*ctx.Registry, m_LightEnv, *ctx.Camera, *ctx.EntityIndex,
                                                          ctx.TransformCache);

                 // 地形阴影深度渲染
                 if (m_ShadowData.HasValidShadowCaster && RendererAPI::GetAPI() != RendererAPI::API::Vulkan)
                 {
                     if (m_ShadowData.CSMActive)
                     {
                         // CSM: 对每个级联渲染地形深度
                         for (int i = 0; i < m_ShadowData.CascadeCount; i++)
                         {
                             m_ShadowSystem.GetSettings(); // 确保 FBO 存在
                             // 绑定对应级联 FBO（重用 Execute 中已绑定的深度）
                             // 这里我们需要单独访问各级联 FBO，通过 ShadowMapFBO fallback
                         }
                     }
                     else
                     {
                         m_ShadowSystem.GetShadowMapFBO()->Bind();
                         RenderCommand::SetCullFaceMode(CullFaceMode::Front);
                         auto depthShader = m_ShadowSystem.GetDepthShader();
                         depthShader->Bind();
                         depthShader->SetMat4("u_LightSpaceMatrix", m_ShadowData.LightSpaceMatrix);
                         m_TerrainSystem.RenderDepth(*ctx.Registry, depthShader, *ctx.EntityIndex, ctx.TransformCache);
                         RenderCommand::SetCullFaceMode(CullFaceMode::Back);
                         m_ShadowSystem.GetShadowMapFBO()->Unbind();
                     }
                 }
             }});

        m_PassQueue.push_back({"TerrainPass", [this](RenderContext& ctx)
                               {
                                   if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
                                   {
                                       static bool s_LoggedUnsupported = false;
                                       if (!s_LoggedUnsupported)
                                       {
                                           s_LoggedUnsupported = true;
                                           ENGINE_CORE_WARN("[Vulkan] Terrain pass disabled until its UBO/descriptor "
                                                            "layout is wired to the scene dispatcher");
                                       }
                                       return;
                                   }

                                   m_TerrainSystem.UpdateTerrainMeshes(*ctx.Registry);
                                   m_TerrainSystem.Render(*ctx.Registry, *ctx.Camera, m_LightEnv, m_ShadowData,
                                                          m_ShadowSystem.GetSettings(), *ctx.EntityIndex,
                                                          ctx.TransformCache);
                               }});

        m_PassQueue.push_back(
            {"GrassPass", [this](RenderContext& ctx)
             {
                 if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
                 {
                     static bool s_LoggedUnsupported = false;
                     if (!s_LoggedUnsupported)
                     {
                         s_LoggedUnsupported = true;
                         ENGINE_CORE_WARN("[Vulkan] Grass pass disabled until instanced scene drawing "
                                          "is implemented");
                     }
                     return;
                 }

                 m_GrassSystem.UpdateGrassData(*ctx.Registry, m_TotalTime);
                 m_GrassSystem.Render(*ctx.Registry, *ctx.Camera, m_LightEnv, m_ShadowData,
                                      m_ShadowSystem.GetSettings(), m_TotalTime, *ctx.EntityIndex, ctx.TransformCache);
             }});

        m_PassQueue.push_back({"SSAOPass", [this](RenderContext& ctx)
                               {
                                   if (!m_SSAOEnabled || ctx.SSAODepthTexID == 0)
                                   {
                                       m_SSAOBlurredTexID = 0;
                                       return;
                                   }

                                   uint32_t vpW = ctx.ViewportWidth;
                                   uint32_t vpH = ctx.ViewportHeight;
                                   if (vpW == 0 || vpH == 0)
                                       return;

                                   uint32_t halfW = std::max(vpW / 2, 1u);
                                   uint32_t halfH = std::max(vpH / 2, 1u);

                                   // 如果分辨率变了，调整 SSAO FBO
                                   auto& ssaoSpec = m_SSAOFBO->GetSpecification();
                                   if (ssaoSpec.Width != halfW || ssaoSpec.Height != halfH)
                                   {
                                       m_SSAOFBO->Resize(halfW, halfH);
                                       m_SSAOBlurFBO->Resize(halfW, halfH);
                                   }

                                   // 生成采样核
                                   static std::vector<glm::vec3> ssaoKernel = GenerateSSAOKernel(64);

                                   int callerFBO = RenderCommand::GetBoundFramebufferID();

                                   // ---- SSAO 采样 ----
                                   m_SSAOFBO->Bind();
                                   RenderCommand::SetViewport(0, 0, halfW, halfH);
                                   RenderCommand::ClearColorOnly();

                                   m_SSAOShader->Bind();

                                   // 绑定深度纹理
                                   RenderCommand::BindTextureUnit(0, ctx.SSAODepthTexID);
                                   m_SSAOShader->SetInt("u_DepthTexture", 0);

                                   // 绑定噪声纹理
                                   m_SSAONoiseTex->Bind(1);
                                   m_SSAOShader->SetInt("u_NoiseTexture", 1);

                                   m_SSAOShader->SetMat4("u_Projection", ctx.Camera->GetProjection());
                                   m_SSAOShader->SetMat4("u_InvProjection", glm::inverse(ctx.Camera->GetProjection()));
                                   m_SSAOShader->SetFloat2("u_ScreenSize", glm::vec2(halfW, halfH));
                                   m_SSAOShader->SetInt("u_KernelSize", std::min(m_SSAOKernelSize, 64));
                                   m_SSAOShader->SetFloat("u_Radius", m_SSAORadius);
                                   m_SSAOShader->SetFloat("u_Bias", m_SSAOBias);
                                   m_SSAOShader->SetFloat("u_Intensity", m_SSAOIntensity);

                                   // 上传采样核
                                   int uploadCount = std::min(m_SSAOKernelSize, 64);
                                   for (int i = 0; i < uploadCount; ++i)
                                   {
                                       std::string name = "u_Samples[" + std::to_string(i) + "]";
                                       m_SSAOShader->SetFloat3(name, ssaoKernel[i]);
                                   }

                                   RenderCommand::SetDepthTest(false);
                                   m_FullscreenQuadVAO->Bind();
                                   RenderCommand::DrawIndexed(m_FullscreenQuadVAO);
                                   RenderCommand::SetDepthTest(true);

                                   m_SSAOFBO->Unbind();

                                   // ---- SSAO 模糊 ----
                                   m_SSAOBlurFBO->Bind();
                                   RenderCommand::SetViewport(0, 0, halfW, halfH);
                                   RenderCommand::ClearColorOnly();

                                   m_SSAOBlurShader->Bind();
                                   RenderCommand::BindTextureUnit(0, m_SSAOFBO->GetColorAttachmentRendererID(0));
                                   m_SSAOBlurShader->SetInt("u_SSAOInput", 0);

                                   RenderCommand::SetDepthTest(false);
                                   m_FullscreenQuadVAO->Bind();
                                   RenderCommand::DrawIndexed(m_FullscreenQuadVAO);
                                   RenderCommand::SetDepthTest(true);

                                   m_SSAOBlurFBO->Unbind();

                                   m_SSAOBlurredTexID = m_SSAOBlurFBO->GetColorAttachmentRendererID(0);

                                   // 恢复调用者 FBO 和视口
                                   RenderCommand::BindFramebufferByID(callerFBO);
                                   RenderCommand::SetViewport(0, 0, vpW, vpH);
                               }});

        m_PassQueue.push_back(
            {"GeometryPass", [this](RenderContext& ctx)
             {
                 // ---- 临时调试（验证后移除）----
                 static bool s_dbgGeo = false;
                 if (!s_dbgGeo)
                 {
                     s_dbgGeo = true;
                     ENGINE_CORE_WARN("[DbgGeo] enter, registry={0}", static_cast<const void*>(ctx.Registry));
                 }

                 PerformanceMonitor::Get().GetSceneRenderGPUTimer().Begin();

                 Renderer::BeginScene(ctx.Camera->GetViewProjection());

                 m_PBRShader->Bind();
                 m_PBRShader->SetFloat3("u_ViewPos", ctx.Camera->GetPosition());
                 m_PBRShader->SetMat4("u_ViewMatrix", ctx.Camera->GetViewMatrix());
                 LightSystem::UploadToShader(m_PBRShader, m_LightEnv);

                 m_PBRShader->SetMat4("u_LightSpaceMatrix", m_ShadowData.LightSpaceMatrix);
                 bool shadowActive = m_ShadowSystem.GetSettings().Enabled && m_ShadowData.HasValidShadowCaster;
                 m_PBRShader->SetInt("u_ShadowEnabled", shadowActive ? 1 : 0);
                 m_PBRShader->SetFloat("u_ShadowBias", m_ShadowSystem.GetSettings().Bias);
                 if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
                 {
                     // 绑本帧实际被渲染的 shadow 资源：CSM 模式下主 shadow map FBO
                     // 从未执行 renderpass（layout 停留 UNDEFINED），须绑级联 0——
                     // 与 ShadowData.ShadowMapTextureID 的 fallback 语义一致
                     const uint32_t shadowIndex = m_ShadowData.CSMActive ? 0 : CSM_MAX_CASCADES;
                     RenderCommand::BindTextureView(1, m_ShadowSystem.GetShadowDepthView(shadowIndex), nullptr);
                 }
                 else
                     RenderCommand::BindTextureUnit(1, m_ShadowData.ShadowMapTextureID);
                 m_PBRShader->SetInt("u_ShadowMap", 1);

                 // CSM 数据上传
                 bool csmActive = shadowActive && m_ShadowData.CSMActive;
                 m_PBRShader->SetInt("u_CSMEnabled", csmActive ? 1 : 0);
                 if (csmActive)
                 {
                     m_PBRShader->SetInt("u_CascadeCount", m_ShadowData.CascadeCount);
                     for (int i = 0; i < m_ShadowData.CascadeCount; i++)
                     {
                         m_PBRShader->SetMat4("u_CascadeLightSpaceMatrices[" + std::to_string(i) + "]",
                                              m_ShadowData.CascadeLightSpaceMatrices[i]);
                         m_PBRShader->SetFloat("u_CascadeSplitDepths[" + std::to_string(i) + "]",
                                               m_ShadowData.CascadeSplitDepths[i]);
                         // 级联阴影纹理绑定到 unit 10~13
                         if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
                             RenderCommand::BindTextureView(10 + i, m_ShadowSystem.GetShadowDepthView(i), nullptr);
                         else
                             RenderCommand::BindTextureUnit(10 + i, m_ShadowData.CascadeShadowMapTexIDs[i]);
                         m_PBRShader->SetInt("u_CascadeShadowMaps[" + std::to_string(i) + "]", 10 + i);
                         m_PBRShader->SetFloat("u_CascadeTexelWorldSize[" + std::to_string(i) + "]",
                                               m_ShadowData.CascadeTexelWorldSizes[i]);
                     }
                 }

                 // IBL 纹理绑定
                 bool iblActive = m_SkyboxSystem.HasIBL();
                 m_PBRShader->SetInt("u_IBLEnabled", iblActive ? 1 : 0);
                 m_PBRShader->SetFloat("u_IBLIntensity", 1.0f);
                 m_PBRShader->SetInt("u_IBLDebugMode", m_IBLDebugMode);
                 if (iblActive)
                 {
                     if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
                     {
                         // Vulkan path: void* 透传 VkImageView + VkSampler，避开 vulkan.h 泄漏 Engine/src/
                         // GetIrradianceMapID() 在 Vulkan 下恒返回 0，必须走 view 接口
                         void* sampler = m_SkyboxSystem.GetIBLSampler();
                         RenderCommand::BindCubemapView(6, m_SkyboxSystem.GetIrradianceView(), sampler);
                         m_PBRShader->SetInt("u_IrradianceMap", 6);
                         RenderCommand::BindCubemapView(7, m_SkyboxSystem.GetPrefilterView(), sampler);
                         m_PBRShader->SetInt("u_PrefilterMap", 7);
                         RenderCommand::BindTextureView(8, m_SkyboxSystem.GetBRDFLutView(), sampler);
                         m_PBRShader->SetInt("u_BRDF_LUT", 8);
                     }
                     else
                     {
                         // OpenGL path：Irradiance/Prefilter cubemap 用 BindCubemapUnit，BRDF LUT 2D 用 BindTextureUnit
                         RenderCommand::BindCubemapUnit(6, m_SkyboxSystem.GetIrradianceMapID());
                         m_PBRShader->SetInt("u_IrradianceMap", 6);
                         RenderCommand::BindCubemapUnit(7, m_SkyboxSystem.GetPrefilterMapID());
                         m_PBRShader->SetInt("u_PrefilterMap", 7);
                         RenderCommand::BindTextureUnit(8, m_SkyboxSystem.GetBRDFLutID());
                         m_PBRShader->SetInt("u_BRDF_LUT", 8);
                     }
                 }

                 // SSAO 纹理绑定
                 bool ssaoActive = m_SSAOEnabled && m_SSAOBlurredTexID != 0;
                 m_PBRShader->SetInt("u_SSAOEnabled", ssaoActive ? 1 : 0);
                 if (ssaoActive)
                 {
                     RenderCommand::BindTextureUnit(9, m_SSAOBlurredTexID);
                     m_PBRShader->SetInt("u_SSAOTexture", 9);
                 }

                 m_RenderQueue.Clear();
                 MeshRenderSystem::SubmitRenderPackets(*ctx.Registry, m_RenderQueue, m_PBRShader, m_WhiteTexture,
                                                       &m_VideoSystem.GetStore(), ctx.EntityIndex, ctx.TransformCache);

                 m_RenderQueue.Flush(ctx.Camera->GetViewProjection());

                 Renderer::EndScene();

                 PerformanceMonitor::Get().GetSceneRenderGPUTimer().End();
             }});

        m_PassQueue.push_back({"SkyboxPass", [this](RenderContext& ctx)
                               { m_SkyboxSystem.Render(ctx.Camera->GetViewMatrix(), ctx.Camera->GetProjection()); }});

        m_PassQueue.push_back(
            {"ParticlePass", [this](RenderContext& ctx)
             {
                 if (!ctx.Registry)
                     return;

                 auto view = ctx.Registry->view<TransformComponent, ParticleEmitterComponent>();

                 for (auto entity : view)
                 {
                     auto&          emitter = view.get<ParticleEmitterComponent>(entity);
                     const uint32_t eid     = static_cast<uint32_t>(entity);
                     const auto     it      = m_ParticleSystems.find(eid);
                     if (it == m_ParticleSystems.end() || !it->second)
                         continue;

                     if (emitter.Blend == ParticleEmitterComponent::BlendMode::Additive)
                         RenderCommand::SetBlendFunc(BlendFactor::SrcAlpha, BlendFactor::One);
                     else
                         RenderCommand::SetBlendFunc(BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha);

                     it->second->Render(ctx.Camera->GetViewMatrix(), ctx.Camera->GetProjection());

                     RenderCommand::SetBlendFunc(BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha);
                 }
             }});

        m_PassQueue.push_back({"FluidPass", [this](RenderContext& ctx)
                               {
                                   if (!ctx.Registry)
                                       return;

                                   auto fluidView = ctx.Registry->view<TransformComponent, FluidEmitterComponent>();
                                   m_MeshSDFFrameStats = {};

                                   for (auto entity : fluidView)
                                   {
                                       auto& emitter = fluidView.get<FluidEmitterComponent>(entity);

                                       // 计算世界坐标（子实体的 Translation 是局部坐标，需变换到世界空间）
                                       glm::mat4 worldMat = WorldTransformService::ComputeWorldTransform(
                                           *ctx.Registry, entity, *ctx.EntityIndex, ctx.TransformCache);
                                       glm::vec3 worldPos = glm::vec3(worldMat[3]);

                                       uint32_t eid    = static_cast<uint32_t>(entity);
                                       auto&    system = m_FluidSystems[eid];

                                       if (!system || system->GetParticleCount() != emitter.ParticleCount)
                                       {
                                           system = CreateRef<FluidSystemGPU>(emitter.ParticleCount);
                                           system->Init();
                                           m_FluidEmitted.erase(eid); // 重建后需要重新发射
                                       }

                                       // 发射策略：lifetime 模式由 Update 内部处理连续发射，
                                       // 否则保持原有一次性/持续发射逻辑
                                       const bool lifetimeMode =
                                           (emitter.EmitRate > 0.0f && emitter.ParticleLifetime > 0.0f);
                                       if (!lifetimeMode)
                                       {
                                           const bool continuousEmit =
                                               (emitter.CurrentPreset == FluidEmitterComponent::Preset::FaucetWater);
                                           if (continuousEmit)
                                           {
                                               system->Emit(worldPos, emitter);
                                           }
                                           else if (m_FluidEmitted.find(eid) == m_FluidEmitted.end())
                                           {
                                               system->Emit(worldPos, emitter);
                                               m_FluidEmitted.insert(eid);
                                           }
                                       }

                                       // 每帧模拟 (lifetime 模式下 emit 在 Update 内部执行)
                                       system->Update(ctx.DeltaTime, worldPos, emitter, ctx.Registry);

                                       const auto& meshStats = system->GetMeshSDFDebugStats();
                                       if (meshStats.Enabled)
                                       {
                                           ++m_MeshSDFFrameStats.ActiveEmitters;
                                           m_MeshSDFFrameStats.BodyCount += meshStats.BodyCount;
                                           m_MeshSDFFrameStats.VoxelCount += meshStats.VoxelCount;
                                           m_MeshSDFFrameStats.EstimatedSamples += meshStats.EstimatedSamples;
                                           m_MeshSDFFrameStats.Resolution =
                                               std::max(m_MeshSDFFrameStats.Resolution, meshStats.Resolution);
                                           m_MeshSDFFrameStats.Band =
                                               std::max(m_MeshSDFFrameStats.Band, meshStats.Band);
                                           m_MeshSDFFrameStats.BuildCpuMs += meshStats.LastBuildCpuMs;
                                       }

                                       // Screen-Space Fluid 渲染
                                       m_FluidRenderer.Render(system->GetParticleBuffer(), system->GetEmptyVAO(),
                                                              emitter.ParticleCount, emitter.ParticleRadius,
                                                              ctx.Camera->GetViewMatrix(), ctx.Camera->GetProjection(),
                                                              ctx.SceneColorTexID, ctx.SceneDepthTexID, emitter);
                                   }
                               }});

        m_FluidRenderer.Init(viewportWidth, viewportHeight);
    }

    void SceneRenderer::Shutdown()
    {
        m_VideoSystem.Shutdown();
        m_AudioSystem.Shutdown();
        m_PassQueue.clear();
        m_GrassSystem.Shutdown();
        m_ParticleSystems.clear();
        m_FluidSystems.clear();
        m_FluidEmitted.clear();
        m_FluidRenderer.Shutdown();
        m_BoundRegistry = nullptr;

        // 清理 SSAO 噪声纹理
        m_SSAONoiseTex.reset();
    }

    void SceneRenderer::BeginScene(const EditorCamera& camera, const SceneRenderInput& input)
    {
        if (m_BoundRegistry != input.Registry)
        {
            m_ParticleSystems.clear();
            m_FluidSystems.clear();
            m_FluidEmitted.clear();
            m_GrassSystem.Shutdown();
            m_BoundRegistry = input.Registry;
        }

        m_Context.Camera         = &camera;
        m_Context.Registry       = input.Registry;
        m_Context.EntityIndex    = input.EntityIndex;
        m_Context.TransformCache = input.TransformCache;
        m_Context.DeltaTime      = input.DeltaTime;
        m_TotalTime += input.DeltaTime;
    }

    void SceneRenderer::Render()
    {
        UpdateParticleSystems();

        for (auto& pass : m_PassQueue)
        {
            if (pass.Enabled)
                pass.ExecuteFn(m_Context);
        }
    }

    void SceneRenderer::EndScene()
    {
        m_Context.Camera         = nullptr;
        m_Context.Registry       = nullptr;
        m_Context.EntityIndex    = nullptr;
        m_Context.TransformCache = nullptr;
        m_Context.DeltaTime      = 0.0f;
    }

    void SceneRenderer::RenderGeometryAndSkybox()
    {
        for (auto& pass : m_PassQueue)
        {
            if (pass.Enabled && (pass.Name == "GeometryPass" || pass.Name == "SkyboxPass" ||
                                 pass.Name == "TerrainPass" || pass.Name == "GrassPass"))
                pass.ExecuteFn(m_Context);
        }
    }

    void SceneRenderer::RenderParticlePass()
    {
        if (!RendererCapabilities::Get().SupportsComputeShaders)
            return;

        for (auto& pass : m_PassQueue)
        {
            if (pass.Enabled && pass.Name == "ParticlePass")
                pass.ExecuteFn(m_Context);
        }
    }

    void SceneRenderer::UpdateParticleSystems()
    {
        if (!RendererCapabilities::Get().SupportsComputeShaders || !m_Context.Registry || !m_Context.EntityIndex)
            return;

        auto view = m_Context.Registry->view<TransformComponent, ParticleEmitterComponent>();
        for (auto entity : view)
        {
            auto& emitter = view.get<ParticleEmitterComponent>(entity);

            const glm::mat4 worldTransform = WorldTransformService::ComputeWorldTransform(
                *m_Context.Registry, entity, *m_Context.EntityIndex, m_Context.TransformCache);
            const glm::vec3 worldPosition = glm::vec3(worldTransform[3]);

            const uint32_t entityID = static_cast<uint32_t>(entity);
            auto&          system   = m_ParticleSystems[entityID];
            if (!system || system->GetMaxParticles() != emitter.MaxParticles)
            {
                system = CreateRef<ParticleSystemGPU>(emitter.MaxParticles);
                system->Init();
            }

            system->Update(m_Context.DeltaTime, worldPosition, emitter, m_Context.Registry);

            // 用户配置的 BurstCount 保持不变，只消费本帧触发量。
            emitter.PendingBurst        = 0;
            emitter.CollisionBurstCount = 0;
        }
    }

    void SceneRenderer::RenderFluidPass()
    {
        if (!RendererCapabilities::Get().SupportsComputeShaders)
            return;

        // Phase 8.2：screen-space 流体的深度/厚度链路在 Vulkan path 未接通，跳过
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
            return;

        for (auto& pass : m_PassQueue)
        {
            if (pass.Enabled && pass.Name == "FluidPass")
                pass.ExecuteFn(m_Context);
        }
    }

    void SceneRenderer::SetHDRFramebuffer(const Ref<Framebuffer>& hdrFBO)
    {
        m_HDRFramebuffer = hdrFBO;
    }

    void SceneRenderer::SetPostProcessing(PostProcessing* pp, PostProcessingSettings* settings)
    {
        m_PostProcessing         = pp;
        m_PostProcessingSettings = settings;
    }

    void SceneRenderer::SetDebugDrawCallback(DebugDrawCallback callback)
    {
        m_DebugDrawCallback = std::move(callback);
    }

    void SceneRenderer::ResizeHDR(uint32_t width, uint32_t height)
    {
        if (m_HDRFramebuffer)
            m_HDRFramebuffer->Resize(width, height);
        if (m_PostProcessing)
            m_PostProcessing->Resize(width, height);
        m_FluidRenderer.Resize(width, height);
    }

    void SceneRenderer::RenderPipeline(const Ref<Framebuffer>& targetFBO)
    {
        // ---- 临时调试：定位视口黑屏（验证后移除）----
        {
            static bool s_dbgLogged = false;
            if (!s_dbgLogged)
            {
                s_dbgLogged = true;
                for (const auto& p : m_PassQueue)
                    ENGINE_CORE_WARN("[DbgPipeline] pass={0} enabled={1}", p.Name, p.Enabled);
            }
        }

        // 设置视口信息
        m_Context.ViewportWidth  = m_HDRFramebuffer->GetSpecification().Width;
        m_Context.ViewportHeight = m_HDRFramebuffer->GetSpecification().Height;

        // Shadow pass（渲染到阴影系统自己的 FBO）— 独立 CPU 计时
        float shadowCpuMs = 0.0f;
        {
            PROFILE_SCOPE("ShadowPass", &shadowCpuMs);
            for (auto& pass : m_PassQueue)
            {
                if (pass.Enabled && (pass.Name == "LightCollect" || pass.Name == "ShadowPass"))
                    pass.ExecuteFn(m_Context);
            }
        }
        PerformanceMonitor::Get().SetShadowPassCPU(shadowCpuMs);

        // compute dispatch、barrier 与异步回读 copy 必须在任何 graphics render pass 之外录制。
        UpdateParticleSystems();

        // 主场景渲染到 HDR FBO
        static bool s_DbgPipelineOnce = false;
        if (!s_DbgPipelineOnce)
        {
            s_DbgPipelineOnce = true;
            const auto& spec  = m_HDRFramebuffer ? m_HDRFramebuffer->GetSpecification() : FramebufferSpecification{};
            ENGINE_CORE_WARN("[DbgRenderPipeline] hdrFbo={0} size=({1},{2})",
                             static_cast<const void*>(m_HDRFramebuffer.get()), spec.Width, spec.Height);
        }
        m_HDRFramebuffer->Bind();
        RenderCommand::SetClearColor({0.1f, 0.1f, 0.1f, 1.0f});
        RenderCommand::Clear();
        m_HDRFramebuffer->ClearAttachment(1, -1);

        const bool msaaEnabled = m_HDRFramebuffer->IsMSAAEnabled();

        // 先写入当前帧几何深度，供后续 SSAO 采样
        for (auto& pass : m_PassQueue)
        {
            bool runPass = pass.Enabled && (pass.Name == "GeometryPass" || pass.Name == "SkyboxPass" ||
                                            pass.Name == "TerrainPass" || pass.Name == "GrassPass");

            if (runPass)
                pass.ExecuteFn(m_Context);
        }

        m_HDRFramebuffer->Unbind();

        // SSAO：在几何渲染完成后执行，使用当前帧的深度
        // GeometryPass 消费上一帧的 m_SSAOBlurredTexID（首帧为 0，自动跳过）
        m_Context.SSAODepthTexID = m_HDRFramebuffer->GetDepthAttachmentRendererID();
        for (auto& pass : m_PassQueue)
        {
            // Phase 8.2：SSAO 链路的深度回读/噪声纹理在 Vulkan path 未接通，跳过
            if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan && pass.Name == "SSAOPass")
                continue;
            if (pass.Enabled && pass.Name == "SSAOPass")
                pass.ExecuteFn(m_Context);
        }

        // MSAA: re-render geometry+skybox to MSAA FBO, then blit
        if (msaaEnabled)
        {
            m_HDRFramebuffer->BindMSAA();
            RenderCommand::SetClearColor({0.1f, 0.1f, 0.1f, 1.0f});
            RenderCommand::Clear();

            RenderGeometryAndSkybox();

            m_HDRFramebuffer->BlitMSAA();

            // Resolve 后单独绘制粒子，避免被 MSAA blit 覆盖掉
            m_HDRFramebuffer->Bind();
            RenderParticlePass();
            m_HDRFramebuffer->Unbind();
        }
        else
        {
            // 非 MSAA 路径需要在 SSAO 生成后重绘场景，确保消费当前帧 AO
            m_HDRFramebuffer->Bind();
            RenderCommand::SetClearColor({0.1f, 0.1f, 0.1f, 1.0f});
            RenderCommand::Clear();
            m_HDRFramebuffer->ClearAttachment(1, -1);

            RenderGeometryAndSkybox();
            RenderParticlePass();
            m_HDRFramebuffer->Unbind();
        }

        // FluidPass: 在粒子绘制完成后、后处理之前执行
        {
            m_HDRFramebuffer->Bind();

            m_Context.SceneColorTexID = m_HDRFramebuffer->GetColorAttachmentRendererID(0);
            m_Context.SceneDepthTexID = m_HDRFramebuffer->GetDepthAttachmentRendererID();

            RenderFluidPass();

            m_HDRFramebuffer->Unbind();
        }

        // 物理碰撞体调试绘制（通过回调，可选）
        if (m_DebugDrawCallback)
        {
            m_HDRFramebuffer->Bind();
            m_DebugDrawCallback();
            m_HDRFramebuffer->Unbind();
        }

        // Post-processing: HDR -> LDR，输出到 targetFBO
        targetFBO->Bind();
        RenderCommand::SetClearColor({0.1f, 0.1f, 0.1f, 1.0f});
        RenderCommand::Clear();
        targetFBO->ClearAttachment(1, -1);

        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
        {
            // Vulkan path：HDR 输入经 view 直通绑槽（slot0=HDR，slot15=bloom 占位），
            // bloom 链依赖 GL ID 传递暂不接通 → 强制关闭
            PostProcessingSettings vkSettings = *m_PostProcessingSettings;
            vkSettings.BloomEnabled           = false;
            RenderCommand::BindTextureView(0, m_HDRFramebuffer->GetColorAttachmentViewHandle(0), nullptr);
            RenderCommand::BindTextureView(15, m_HDRFramebuffer->GetColorAttachmentViewHandle(0), nullptr);
            m_PostProcessing->Process(m_HDRFramebuffer->GetColorAttachmentRendererID(0), vkSettings);
        }
        else
        {
            m_PostProcessing->Process(m_HDRFramebuffer->GetColorAttachmentRendererID(0), *m_PostProcessingSettings);
        }

        targetFBO->Unbind();
    }
    void SceneRenderer::RenderEditorPicking(const Ref<Framebuffer>& pickingFBO)
    {
        if (!pickingFBO || !m_Context.Camera || !m_Context.Registry)
            return;

        // Phase 8.2：Vulkan path 的拾取回读未接通（RED_INTEGER 单 attachment pass
        // 与 PBR 双输出 pipeline 不兼容），跳过整段绘制。
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
            return;

        pickingFBO->Bind();
        RenderCommand::SetClearColor({0.0f, 0.0f, 0.0f, 1.0f});
        RenderCommand::Clear();
        pickingFBO->ClearAttachment(1, -1);

        RenderGeometryAndSkybox();

        pickingFBO->Unbind();
    }

    void SceneRenderer::ReleaseParticleSystem(uint32_t entityID)
    {
        m_ParticleSystems.erase(entityID);
    }

    void SceneRenderer::ReleaseFluidSystem(uint32_t entityID)
    {
        m_FluidSystems.erase(entityID);
        m_FluidEmitted.erase(entityID);
    }

} // namespace Engine
