#include "engpch.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/EditorCamera.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/Systems/MeshRenderSystem.h"
#include "Debug/PerformanceMonitor.h"

namespace Engine
{

    void SceneRenderer::Init()
    {
        m_PBRShader = Shader::Create("assets/shaders/PBR.glsl");

        m_WhiteTexture = Texture2D::Create(1, 1);
        uint32_t whiteData = 0xFFFFFFFF;
        m_WhiteTexture->SetData(&whiteData, sizeof(uint32_t));

        m_ShadowSystem.Init();
        m_SkyboxSystem.Init();

        // 注册 Pass 队列
        m_PassQueue.push_back({"LightCollect", [this](RenderContext& ctx) {
            m_LightEnv = LightSystem::CollectLights(ctx.ActiveScene->GetRegistry());
        }});

        m_PassQueue.push_back({"ShadowPass", [this](RenderContext& ctx) {
            m_ShadowData = m_ShadowSystem.Execute(ctx.ActiveScene->GetRegistry(), m_LightEnv);
        }});

        m_PassQueue.push_back({"GeometryPass", [this](RenderContext& ctx) {
            PerformanceMonitor::Get().GetSceneRenderGPUTimer().Begin();

            Renderer::BeginScene(ctx.Camera->GetViewProjection());

            // Upload lights + shadow to PBR shader
            m_PBRShader->Bind();
            m_PBRShader->SetFloat3("u_ViewPos", ctx.Camera->GetPosition());
            LightSystem::UploadToShader(m_PBRShader, m_LightEnv);

            // Shadow uniforms
            m_PBRShader->SetMat4("u_LightSpaceMatrix", m_ShadowData.LightSpaceMatrix);
            bool shadowActive = m_ShadowSystem.GetSettings().Enabled && m_ShadowData.HasValidShadowCaster;
            m_PBRShader->SetInt("u_ShadowEnabled", shadowActive ? 1 : 0);
            m_PBRShader->SetFloat("u_ShadowBias", m_ShadowSystem.GetSettings().Bias);
            RenderCommand::BindTextureUnit(1, m_ShadowData.ShadowMapTextureID);
            m_PBRShader->SetInt("u_ShadowMap", 1);

            // Submit mesh render packets
            m_RenderQueue.Clear();
            MeshRenderSystem::SubmitRenderPackets(ctx.ActiveScene->GetRegistry(), m_RenderQueue,
                                                    m_PBRShader, m_WhiteTexture);

            // Flush: bind each material + set per-draw VP/Transform + draw
            m_RenderQueue.Flush(ctx.Camera->GetViewProjection());

            Renderer::EndScene();

            PerformanceMonitor::Get().GetSceneRenderGPUTimer().End();
        }});

        m_PassQueue.push_back({"SkyboxPass", [this](RenderContext& ctx) {
            m_SkyboxSystem.Render(ctx.Camera->GetViewMatrix(), ctx.Camera->GetProjection());
        }});
    }

    void SceneRenderer::Shutdown()
    {
        m_PassQueue.clear();
    }

    void SceneRenderer::BeginScene(const EditorCamera& camera, Scene* scene)
    {
        m_Context.Camera = const_cast<EditorCamera*>(&camera);
        m_Context.ActiveScene = scene;
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
    }

    void SceneRenderer::RenderGeometryAndSkybox()
    {
        // MSAA 重绘时只走 Geometry + Skybox，不重新收集光照/阴影
        for (auto& pass : m_PassQueue)
        {
            if (pass.Enabled && (pass.Name == "GeometryPass" || pass.Name == "SkyboxPass"))
                pass.ExecuteFn(m_Context);
        }
    }

} // namespace Engine
