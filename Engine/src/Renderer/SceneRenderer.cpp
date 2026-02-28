#include "engpch.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/RendererAPI.h"
#include "Renderer/RendererCapabilities.h"
#include "Renderer/EditorCamera.h"
#include "Asset/AssetManager.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/Systems/MeshRenderSystem.h"
#include "Debug/PerformanceMonitor.h"

namespace Engine
{

    void SceneRenderer::Init()
    {
        m_PBRShader = Shader::Create("assets/shaders/PBR.glsl");

        m_WhiteTextureHandle = AssetManager::Load<Texture2D>("builtin:white");
        m_WhiteTexture = AssetManager::GetRef<Texture2D>(m_WhiteTextureHandle);

        m_ShadowSystem.Init();
        m_SkyboxSystem.Init();
        m_TerrainSystem.Init();
        m_AudioSystem.Init();
        m_VideoSystem.Init();

        m_PassQueue.push_back({"LightCollect", [this](RenderContext& ctx) {
            m_LightEnv = LightSystem::CollectLights(ctx.ActiveScene->GetRegistry());
        }});

        m_PassQueue.push_back({"ShadowPass", [this](RenderContext& ctx) {
            m_ShadowData = m_ShadowSystem.Execute(ctx.ActiveScene->GetRegistry(), m_LightEnv);

            // 地形阴影深度渲染
            if (m_ShadowData.HasValidShadowCaster)
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
        }});

        m_PassQueue.push_back({"TerrainPass", [this](RenderContext& ctx) {
            m_TerrainSystem.UpdateTerrainMeshes(ctx.ActiveScene->GetRegistry());
            m_TerrainSystem.Render(ctx.ActiveScene->GetRegistry(), *ctx.Camera,
                m_LightEnv, m_ShadowData, m_ShadowSystem.GetSettings());
        }});

        m_PassQueue.push_back({"GeometryPass", [this](RenderContext& ctx) {
            PerformanceMonitor::Get().GetSceneRenderGPUTimer().Begin();

            Renderer::BeginScene(ctx.Camera->GetViewProjection());

            m_PBRShader->Bind();
            m_PBRShader->SetFloat3("u_ViewPos", ctx.Camera->GetPosition());
            LightSystem::UploadToShader(m_PBRShader, m_LightEnv);

            m_PBRShader->SetMat4("u_LightSpaceMatrix", m_ShadowData.LightSpaceMatrix);
            bool shadowActive = m_ShadowSystem.GetSettings().Enabled && m_ShadowData.HasValidShadowCaster;
            m_PBRShader->SetInt("u_ShadowEnabled", shadowActive ? 1 : 0);
            m_PBRShader->SetFloat("u_ShadowBias", m_ShadowSystem.GetSettings().Bias);
            RenderCommand::BindTextureUnit(1, m_ShadowData.ShadowMapTextureID);
            m_PBRShader->SetInt("u_ShadowMap", 1);

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
    }

    void SceneRenderer::Shutdown()
    {
        m_VideoSystem.Shutdown();
        m_AudioSystem.Shutdown();
        m_PassQueue.clear();
        m_ParticleSystems.clear();
    }

    void SceneRenderer::BeginScene(const EditorCamera& camera, Scene* scene, float deltaTime)
    {
        if (m_LastScene != scene)
        {
            m_ParticleSystems.clear();
            m_LastScene = scene;
        }

        m_Context.Camera = const_cast<EditorCamera*>(&camera);
        m_Context.ActiveScene = scene;
        m_Context.DeltaTime = deltaTime;
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
            if (pass.Enabled && (pass.Name == "GeometryPass" || pass.Name == "SkyboxPass" || pass.Name == "TerrainPass"))
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

} // namespace Engine
