#include "engpch.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/RendererAPI.h"
#include "Renderer/RendererCapabilities.h"
#include "Renderer/EditorCamera.h"
#include "Renderer/Buffer.h"
#include "Asset/AssetManager.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/Systems/MeshRenderSystem.h"
#include "Debug/PerformanceMonitor.h"

#include <glad/gl.h>
#include <random>

namespace Engine
{

    // 生成 SSAO 采样核
    static std::vector<glm::vec3> GenerateSSAOKernel(int kernelSize)
    {
        std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
        std::default_random_engine generator;

        std::vector<glm::vec3> ssaoKernel;
        for (int i = 0; i < kernelSize; ++i)
        {
            glm::vec3 sample(
                randomFloats(generator) * 2.0f - 1.0f,
                randomFloats(generator) * 2.0f - 1.0f,
                randomFloats(generator)  // 半球：z >= 0
            );
            sample = glm::normalize(sample);
            sample *= randomFloats(generator);

            // 加速插值：让更多采样点靠近原点
            float scale = static_cast<float>(i) / static_cast<float>(kernelSize);
            scale = 0.1f + scale * scale * 0.9f;  // lerp(0.1, 1.0, scale^2)
            sample *= scale;

            ssaoKernel.push_back(sample);
        }
        return ssaoKernel;
    }

    void SceneRenderer::Init(uint32_t viewportWidth, uint32_t viewportHeight)
    {
        m_PBRShader = Shader::Create("assets/shaders/PBR.glsl");

        m_WhiteTextureHandle = AssetManager::Load<Texture2D>("builtin:white");
        m_WhiteTexture = AssetManager::GetRef<Texture2D>(m_WhiteTextureHandle);

        m_ShadowSystem.Init();
        m_SkyboxSystem.Init();
        m_TerrainSystem.Init();
        m_GrassSystem.Init();
        m_AudioSystem.Init();
        m_VideoSystem.Init();

        // ---- SSAO 初始化 ----
        m_SSAOShader = Shader::Create("assets/shaders/SSAO.glsl");
        m_SSAOBlurShader = Shader::Create("assets/shaders/SSAOBlur.glsl");

        // SSAO FBO（R16F 单通道，半分辨率）
        {
            uint32_t halfW = std::max(viewportWidth / 2, 1u);
            uint32_t halfH = std::max(viewportHeight / 2, 1u);

            FramebufferSpecification ssaoSpec;
            ssaoSpec.Attachments = {FramebufferTextureFormat::R16F};
            ssaoSpec.Width = halfW;
            ssaoSpec.Height = halfH;
            m_SSAOFBO = Framebuffer::Create(ssaoSpec);

            FramebufferSpecification ssaoBlurSpec;
            ssaoBlurSpec.Attachments = {FramebufferTextureFormat::R16F};
            ssaoBlurSpec.Width = halfW;
            ssaoBlurSpec.Height = halfH;
            m_SSAOBlurFBO = Framebuffer::Create(ssaoBlurSpec);
        }

        // SSAO 4x4 噪声纹理
        {
            std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
            std::default_random_engine generator;
            std::vector<glm::vec3> ssaoNoise;
            for (int i = 0; i < 16; i++)
            {
                glm::vec3 noise(
                    randomFloats(generator) * 2.0f - 1.0f,
                    randomFloats(generator) * 2.0f - 1.0f,
                    0.0f
                );
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
                -1.0f, -1.0f,   0.0f, 0.0f,
                 1.0f, -1.0f,   1.0f, 0.0f,
                 1.0f,  1.0f,   1.0f, 1.0f,
                -1.0f,  1.0f,   0.0f, 1.0f,
            };
            uint32_t quadIndices[] = {0, 1, 2, 2, 3, 0};

            m_FullscreenQuadVAO = VertexArray::Create();
            auto vb = VertexBuffer::Create(quadVertices, sizeof(quadVertices));
            vb->SetLayout({
                {ShaderDataType::Float2, "a_Position"},
                {ShaderDataType::Float2, "a_TexCoord"},
            });
            m_FullscreenQuadVAO->AddVertexBuffer(vb);
            auto ib = IndexBuffer::Create(quadIndices, 6);
            m_FullscreenQuadVAO->SetIndexBuffer(ib);
        }

        m_PassQueue.push_back({"LightCollect", [this](RenderContext& ctx) {
            m_LightEnv = LightSystem::CollectLights(ctx.ActiveScene->GetRegistry());
        }});

        m_PassQueue.push_back({"ShadowPass", [this](RenderContext& ctx) {
            // 使用 CSM 版本
            m_ShadowData = m_ShadowSystem.ExecuteCSM(
                ctx.ActiveScene->GetRegistry(), m_LightEnv, *ctx.Camera);

            // 地形阴影深度渲染
            if (m_ShadowData.HasValidShadowCaster)
            {
                if (m_ShadowData.CSMActive)
                {
                    // CSM: 对每个级联渲染地形深度
                    for (int i = 0; i < m_ShadowData.CascadeCount; i++)
                    {
                        m_ShadowSystem.GetSettings();  // 确保 FBO 存在
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
                    m_TerrainSystem.RenderDepth(ctx.ActiveScene->GetRegistry(), depthShader);
                    RenderCommand::SetCullFaceMode(CullFaceMode::Back);
                    m_ShadowSystem.GetShadowMapFBO()->Unbind();
                }
            }
        }});

        m_PassQueue.push_back({"TerrainPass", [this](RenderContext& ctx) {
            m_TerrainSystem.UpdateTerrainMeshes(ctx.ActiveScene->GetRegistry());
            m_TerrainSystem.Render(ctx.ActiveScene->GetRegistry(), *ctx.Camera,
                m_LightEnv, m_ShadowData, m_ShadowSystem.GetSettings());
        }});

        m_PassQueue.push_back({"GrassPass", [this](RenderContext& ctx) {
            m_GrassSystem.UpdateGrassData(ctx.ActiveScene->GetRegistry(), m_TotalTime);
            m_GrassSystem.Render(ctx.ActiveScene->GetRegistry(), *ctx.Camera,
                m_LightEnv, m_ShadowData, m_ShadowSystem.GetSettings(), m_TotalTime);
        }});

        m_PassQueue.push_back({"SSAOPass", [this](RenderContext& ctx) {
            if (!m_SSAOEnabled || ctx.SSAODepthTexID == 0)
            {
                m_SSAOBlurredTexID = 0;
                return;
            }

            uint32_t vpW = ctx.ViewportWidth;
            uint32_t vpH = ctx.ViewportHeight;
            if (vpW == 0 || vpH == 0) return;

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

        m_PassQueue.push_back({"GeometryPass", [this](RenderContext& ctx) {
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
                }
            }

            // IBL 纹理绑定
            bool iblActive = m_SkyboxSystem.HasIBL();
            m_PBRShader->SetInt("u_IBLEnabled", iblActive ? 1 : 0);
            m_PBRShader->SetFloat("u_IBLIntensity", 1.0f);
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
            MeshRenderSystem::SubmitRenderPackets(ctx.ActiveScene->GetRegistry(), m_RenderQueue,
                                                    m_PBRShader, m_WhiteTexture);

            m_RenderQueue.Flush(ctx.Camera->GetViewProjection());

            Renderer::EndScene();

            PerformanceMonitor::Get().GetSceneRenderGPUTimer().End();
        }});

        m_PassQueue.push_back({"SkyboxPass", [this](RenderContext& ctx) {
            m_SkyboxSystem.Render(ctx.Camera->GetViewMatrix(), ctx.Camera->GetProjection());
        }});

        m_PassQueue.push_back({"ParticlePass", [this](RenderContext& ctx) {
            if (!ctx.ActiveScene) return;

            auto view = ctx.ActiveScene->GetRegistry().view<TransformComponent, ParticleEmitterComponent>();

            for (auto entity : view)
            {
                auto& transform = view.get<TransformComponent>(entity);
                auto& emitter = view.get<ParticleEmitterComponent>(entity);

                uint32_t eid = static_cast<uint32_t>(entity);
                auto& system = m_ParticleSystems[eid];

                if (!system || system->GetMaxParticles() != emitter.MaxParticles)
                {
                    system = CreateRef<ParticleSystemGPU>(emitter.MaxParticles);
                    system->Init();
                }

                system->Update(ctx.DeltaTime, transform.Translation, emitter, &ctx.ActiveScene->GetRegistry());

                if (emitter.Blend == ParticleEmitterComponent::BlendMode::Additive)
                    RenderCommand::SetBlendFunc(BlendFactor::SrcAlpha, BlendFactor::One);
                else
                    RenderCommand::SetBlendFunc(BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha);

                system->Render(ctx.Camera->GetViewMatrix(), ctx.Camera->GetProjection());

                RenderCommand::SetBlendFunc(BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha);

                // 重置本帧触发的爆发（用户配置的 BurstCount 保持不变）
                emitter.PendingBurst = 0;
                emitter.CollisionBurstCount = 0;
            }
        }});

        m_PassQueue.push_back({"FluidPass", [this](RenderContext& ctx) {
            if (!ctx.ActiveScene) return;

            auto fluidView = ctx.ActiveScene->GetRegistry().view<TransformComponent, FluidEmitterComponent>();

            for (auto entity : fluidView)
            {
                auto& transform = fluidView.get<TransformComponent>(entity);
                auto& emitter = fluidView.get<FluidEmitterComponent>(entity);

                uint32_t eid = static_cast<uint32_t>(entity);
                auto& system = m_FluidSystems[eid];

                if (!system || system->GetParticleCount() != emitter.ParticleCount)
                {
                    system = CreateRef<FluidSystemGPU>(emitter.ParticleCount);
                    system->Init();
                    emitter.Emitted = false;  // 重建后需要重新发射
                }

                // 首次发射
                if (!emitter.Emitted)
                {
                    system->Emit(transform.Translation, emitter);
                    emitter.Emitted = true;
                }

                // 每帧模拟
                system->Update(ctx.DeltaTime, transform.Translation, emitter,
                               &ctx.ActiveScene->GetRegistry());

                // Screen-Space Fluid 渲染
                m_FluidRenderer.Render(
                    system->GetParticleBuffer(), system->GetEmptyVAO(),
                    emitter.ParticleCount, emitter.ParticleRadius,
                    ctx.Camera->GetViewMatrix(), ctx.Camera->GetProjection(),
                    ctx.SceneColorTexID, ctx.SceneDepthTexID,
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
        m_FluidRenderer.Shutdown();

        // 清理 SSAO 噪声纹理
        if (m_SSAONoiseTexID)
        {
            glDeleteTextures(1, &m_SSAONoiseTexID);
            m_SSAONoiseTexID = 0;
        }
    }

    void SceneRenderer::BeginScene(const EditorCamera& camera, Scene* scene, float deltaTime)
    {
        if (m_LastScene != scene)
        {
            m_ParticleSystems.clear();
            m_FluidSystems.clear();
            m_GrassSystem.Shutdown();
            m_LastScene = scene;
        }

        m_Context.Camera = const_cast<EditorCamera*>(&camera);
        m_Context.ActiveScene = scene;
        m_Context.DeltaTime = deltaTime;
        m_TotalTime += deltaTime;
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
        m_Context.Camera = nullptr;
        m_Context.ActiveScene = nullptr;
        m_Context.DeltaTime = 0.0f;
    }

    void SceneRenderer::RenderGeometryAndSkybox()
    {
        for (auto& pass : m_PassQueue)
        {
            if (pass.Enabled && (pass.Name == "GeometryPass" || pass.Name == "SkyboxPass" || pass.Name == "TerrainPass" || pass.Name == "GrassPass"))
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

} // namespace Engine
