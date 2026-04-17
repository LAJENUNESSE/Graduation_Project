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

#include <glad/gl.h>
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

            glGenTextures(1, &m_SSAONoiseTexID);
            glBindTexture(GL_TEXTURE_2D, m_SSAONoiseTexID);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 4, 4, 0, GL_RGB, GL_FLOAT, ssaoNoise.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
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

        m_PassQueue.push_back({"ShadowPass", [this](RenderContext& ctx)
                               {
                                   // 使用 CSM 版本
                                   m_ShadowData = m_ShadowSystem.ExecuteCSM(*ctx.Registry, m_LightEnv, *ctx.Camera,
                                                                            *ctx.EntityIndex, ctx.TransformCache);

                                   // 地形阴影深度渲染
                                   if (m_ShadowData.HasValidShadowCaster)
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
                                           m_TerrainSystem.RenderDepth(*ctx.Registry, depthShader, *ctx.EntityIndex,
                                                                       ctx.TransformCache);
                                           RenderCommand::SetCullFaceMode(CullFaceMode::Back);
                                           m_ShadowSystem.GetShadowMapFBO()->Unbind();
                                       }
                                   }
                               }});

        m_PassQueue.push_back({"TerrainPass", [this](RenderContext& ctx)
                               {
                                   m_TerrainSystem.UpdateTerrainMeshes(*ctx.Registry);
                                   m_TerrainSystem.Render(*ctx.Registry, *ctx.Camera, m_LightEnv, m_ShadowData,
                                                          m_ShadowSystem.GetSettings(), *ctx.EntityIndex,
                                                          ctx.TransformCache);
                               }});

        m_PassQueue.push_back({"GrassPass", [this](RenderContext& ctx)
                               {
                                   m_GrassSystem.UpdateGrassData(*ctx.Registry, m_TotalTime);
                                   m_GrassSystem.Render(*ctx.Registry, *ctx.Camera, m_LightEnv, m_ShadowData,
                                                        m_ShadowSystem.GetSettings(), m_TotalTime, *ctx.EntityIndex,
                                                        ctx.TransformCache);
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
                                   RenderCommand::BindTextureUnit(1, m_SSAONoiseTexID);
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
                     // Irradiance 和 Prefilter 是 cubemap 纹理，必须用 BindCubemapUnit
                     RenderCommand::BindCubemapUnit(6, m_SkyboxSystem.GetIrradianceMapID());
                     m_PBRShader->SetInt("u_IrradianceMap", 6);
                     RenderCommand::BindCubemapUnit(7, m_SkyboxSystem.GetPrefilterMapID());
                     m_PBRShader->SetInt("u_PrefilterMap", 7);
                     // BRDF LUT 是 2D 纹理，使用 BindTextureUnit
                     RenderCommand::BindTextureUnit(8, m_SkyboxSystem.GetBRDFLutID());
                     m_PBRShader->SetInt("u_BRDF_LUT", 8);
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
                     auto& emitter = view.get<ParticleEmitterComponent>(entity);

                     // 计算世界坐标（子实体的 Translation 是局部坐标，需变换到世界空间）
                     glm::mat4 worldMat = WorldTransformService::ComputeWorldTransform(
                         *ctx.Registry, entity, *ctx.EntityIndex, ctx.TransformCache);
                     glm::vec3 worldPos = glm::vec3(worldMat[3]);

                     uint32_t eid    = static_cast<uint32_t>(entity);
                     auto&    system = m_ParticleSystems[eid];

                     if (!system || system->GetMaxParticles() != emitter.MaxParticles)
                     {
                         system = CreateRef<ParticleSystemGPU>(emitter.MaxParticles);
                         system->Init();
                     }

                     system->Update(ctx.DeltaTime, worldPos, emitter, ctx.Registry);

                     if (emitter.Blend == ParticleEmitterComponent::BlendMode::Additive)
                         RenderCommand::SetBlendFunc(BlendFactor::SrcAlpha, BlendFactor::One);
                     else
                         RenderCommand::SetBlendFunc(BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha);

                     system->Render(ctx.Camera->GetViewMatrix(), ctx.Camera->GetProjection());

                     RenderCommand::SetBlendFunc(BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha);

                     // 重置本帧触发的爆发（用户配置的 BurstCount 保持不变）
                     emitter.PendingBurst        = 0;
                     emitter.CollisionBurstCount = 0;
                 }
             }});

        m_PassQueue.push_back(
            {"FluidPass", [this](RenderContext& ctx)
             {
                 if (!ctx.Registry)
                     return;

                 auto fluidView      = ctx.Registry->view<TransformComponent, FluidEmitterComponent>();
                 m_MeshSDFFrameStats = {};

                 // [DIAG] 诊断日志：每秒限频打印 FluidEmitter 状态
                 // 原先的 static bool 在 scene 加载前被消耗，失效了。改为累加 DeltaTime 的限频策略。
                 static float    s_FluidPassLogAcc = 0.0f;
                 static uint32_t s_FluidPassFrame  = 0;
                 s_FluidPassLogAcc += ctx.DeltaTime;
                 ++s_FluidPassFrame;
                 const bool shouldLog = (s_FluidPassLogAcc >= 1.0f);
                 if (shouldLog)
                     s_FluidPassLogAcc = 0.0f;

                 uint32_t     entityCount = 0;
                 entt::entity firstEntity = entt::null;
                 for (auto e : fluidView)
                 {
                     if (firstEntity == entt::null)
                         firstEntity = e;
                     ++entityCount;
                 }

                 if (shouldLog)
                 {
                     if (entityCount == 0)
                     {
                         ENGINE_WARN("[FluidPass][diag] frame={} entities=0 viewport={}x{}", s_FluidPassFrame,
                                     ctx.ViewportWidth, ctx.ViewportHeight);
                     }
                     else
                     {
                         auto&     emitter0  = fluidView.get<FluidEmitterComponent>(firstEntity);
                         glm::mat4 worldMat0 = WorldTransformService::ComputeWorldTransform(
                             *ctx.Registry, firstEntity, *ctx.EntityIndex, ctx.TransformCache);
                         glm::vec3 worldPos0 = glm::vec3(worldMat0[3]);
                         uint32_t  eid0      = static_cast<uint32_t>(firstEntity);
                         glm::vec3 camPos(0.0f), camFwd(0.0f, 0.0f, -1.0f);
                         float     distToEmitter = -1.0f;
                         if (ctx.Camera)
                         {
                             camPos        = ctx.Camera->GetPosition();
                             camFwd        = ctx.Camera->GetForwardDirection();
                             distToEmitter = glm::length(worldPos0 - camPos);
                         }
                         ENGINE_WARN("[FluidPass][diag] frame={} entities={} eid0={} "
                                     "worldPos=({:.3f},{:.3f},{:.3f}) particleCount={} "
                                     "radius={:.4f} preset={} useBoundary={} "
                                     "bMin=({:.2f},{:.2f},{:.2f}) bMax=({:.2f},{:.2f},{:.2f}) "
                                     "emitted={} viewport={}x{} "
                                     "camPos=({:.3f},{:.3f},{:.3f}) camFwd=({:.3f},{:.3f},{:.3f}) "
                                     "distToEmitter={:.3f}",
                                     s_FluidPassFrame, entityCount, eid0, worldPos0.x, worldPos0.y, worldPos0.z,
                                     emitter0.ParticleCount, emitter0.ParticleRadius,
                                     static_cast<int>(emitter0.CurrentPreset), emitter0.UseBoundary ? 1 : 0,
                                     emitter0.BoundaryMin.x, emitter0.BoundaryMin.y, emitter0.BoundaryMin.z,
                                     emitter0.BoundaryMax.x, emitter0.BoundaryMax.y, emitter0.BoundaryMax.z,
                                     m_FluidEmitted.find(eid0) != m_FluidEmitted.end() ? 1 : 0, ctx.ViewportWidth,
                                     ctx.ViewportHeight, camPos.x, camPos.y, camPos.z, camFwd.x, camFwd.y, camFwd.z,
                                     distToEmitter);
                     }
                 }

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

                     // 发射策略：水龙头预设持续发射，其他预设保持一次性发射
                     const bool continuousEmit = (emitter.CurrentPreset == FluidEmitterComponent::Preset::FaucetWater);
                     if (continuousEmit)
                     {
                         system->Emit(worldPos, emitter);
                     }
                     else if (m_FluidEmitted.find(eid) == m_FluidEmitted.end())
                     {
                         system->Emit(worldPos, emitter);
                         m_FluidEmitted.insert(eid);
                     }

                     // 每帧模拟
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
                         m_MeshSDFFrameStats.Band = std::max(m_MeshSDFFrameStats.Band, meshStats.Band);
                         m_MeshSDFFrameStats.BuildCpuMs += meshStats.LastBuildCpuMs;
                     }

                     // Screen-Space Fluid 渲染
                     m_FluidRenderer.Render(system->GetParticleBuffer(), system->GetEmptyVAO(), emitter.ParticleCount,
                                            emitter.ParticleRadius, ctx.Camera->GetViewMatrix(),
                                            ctx.Camera->GetProjection(), ctx.SceneColorTexID, ctx.SceneDepthTexID,
                                            emitter);
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
        if (m_SSAONoiseTexID)
        {
            glDeleteTextures(1, &m_SSAONoiseTexID);
            m_SSAONoiseTexID = 0;
        }
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

    void SceneRenderer::RenderFluidPass()
    {
        if (!RendererCapabilities::Get().SupportsComputeShaders)
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

        // 主场景渲染到 HDR FBO
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
        RenderCommand::SetClearColor({0.0f, 0.0f, 0.0f, 1.0f});
        RenderCommand::Clear();
        targetFBO->ClearAttachment(1, -1);

        m_PostProcessing->Process(m_HDRFramebuffer->GetColorAttachmentRendererID(0), *m_PostProcessingSettings);

        targetFBO->Unbind();
    }
    void SceneRenderer::RenderEditorPicking(const Ref<Framebuffer>& pickingFBO)
    {
        if (!pickingFBO || !m_Context.Camera || !m_Context.Registry)
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
