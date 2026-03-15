#include "EditorRenderController.h"

#include "Debug/PerformanceMonitor.h"
#include "Debug/ProfileTimer.h"
#include "EditorPanelCoordinator.h"
#include "EditorViewportController.h"
#include "Physics/PhysicsDebugDraw.h"
#include "Renderer/PostProcessing.h"
#include "Renderer/SceneRenderInput.h"
#include "Renderer/SceneRenderer.h"
#include "Scene/Scene.h"

namespace Engine
{

    void EditorRenderController::Initialize(SceneRenderer* sceneRenderer, PostProcessing* postProcessing,
                                            PostProcessingSettings* postProcessingSettings,
                                            EditorViewportController* viewportController,
                                            EditorPanelCoordinator* panelCoordinator,
                                            PhysicsDebugDraw* physicsDebugDraw, bool* showPhysicsColliders)
    {
        m_SceneRenderer = sceneRenderer;
        m_PostProcessing = postProcessing;
        m_PostProcessingSettings = postProcessingSettings;
        m_ViewportController = viewportController;
        m_PanelCoordinator = panelCoordinator;
        m_PhysicsDebugDraw = physicsDebugDraw;
        m_ShowPhysicsColliders = showPhysicsColliders;
    }

    void EditorRenderController::Attach()
    {
        const auto& viewportContext = m_ViewportController->GetContext();
        const uint32_t width = static_cast<uint32_t>(viewportContext.RenderSize.x);
        const uint32_t height = static_cast<uint32_t>(viewportContext.RenderSize.y);

        m_PostProcessing->Init(width, height);
        m_SceneRenderer->Init(width, height);
        m_SceneRenderer->SetPostProcessing(m_PostProcessing, m_PostProcessingSettings);
        m_ViewportController->SetResizeCallback([this](uint32_t resizedWidth, uint32_t resizedHeight)
                                                { OnViewportResized(resizedWidth, resizedHeight); });
        m_SceneRenderer->SetDebugDrawCallback(
            [this]()
            {
                if (!m_ActiveScene || !m_ShowPhysicsColliders || !*m_ShowPhysicsColliders)
                    return;

                m_PhysicsDebugDraw->DrawColliders(m_ActiveScene->GetRegistry(), m_ViewportController->GetCamera());
            });

        SyncHDRFramebufferBindings();
    }

    void EditorRenderController::Detach()
    {
        m_ViewportController->SetResizeCallback({});
        m_ActiveScene = nullptr;
        m_SceneRenderer->Shutdown();
    }

    void EditorRenderController::OnUpdate(Timestep ts, const Ref<Scene>& activeScene, SceneState sceneState)
    {
        m_ActiveScene = activeScene.get();

        switch (sceneState)
        {
        case SceneState::Edit:
            activeScene->OnUpdateEditor(ts, m_ViewportController->GetCamera());
            break;
        case SceneState::Play:
            activeScene->OnUpdateRuntime(ts, m_ViewportController->GetCamera());
            break;
        }

        float sceneRenderCpuMs = 0.0f;
        {
            PROFILE_SCOPE("SceneRender", &sceneRenderCpuMs);
            SceneRenderInput input;
            input.Registry = &activeScene->GetRegistry();
            input.EntityIndex = &activeScene->GetEntityIndex();
            input.DeltaTime = ts;
            input.TransformCache = &activeScene->GetTransformCache();
            m_SceneRenderer->BeginScene(m_ViewportController->GetCamera(), input);
            m_SceneRenderer->RenderPipeline(m_ViewportController->GetFramebuffer());
            if (sceneState == SceneState::Edit)
                m_SceneRenderer->RenderEditorPicking(m_ViewportController->GetPickingFramebuffer());
            m_SceneRenderer->EndScene();
        }
        PerformanceMonitor::Get().SetSceneRenderCPU(sceneRenderCpuMs);
    }

    void EditorRenderController::ApplyMSAASamples(uint32_t samples)
    {
        m_ViewportController->ApplyMSAASamples(samples);
        SyncHDRFramebufferBindings();
    }

    void EditorRenderController::SyncSSAOSettings(EditorRenderSettings& settings, bool toRenderer)
    {
        if (toRenderer)
        {
            m_SceneRenderer->GetSSAOEnabled() = settings.SSAOEnabled;
            m_SceneRenderer->GetSSAORadius() = settings.SSAORadius;
            m_SceneRenderer->GetSSAOBias() = settings.SSAOBias;
            m_SceneRenderer->GetSSAOKernelSize() = settings.SSAOKernelSize;
            m_SceneRenderer->GetSSAOIntensity() = settings.SSAOIntensity;
        }
        else
        {
            settings.SSAOEnabled = m_SceneRenderer->GetSSAOEnabled();
            settings.SSAORadius = m_SceneRenderer->GetSSAORadius();
            settings.SSAOBias = m_SceneRenderer->GetSSAOBias();
            settings.SSAOKernelSize = m_SceneRenderer->GetSSAOKernelSize();
            settings.SSAOIntensity = m_SceneRenderer->GetSSAOIntensity();
        }
    }

    void EditorRenderController::ApplyRenderSettings(const Ref<Scene>& activeScene,
                                                     const EditorRenderSettings& renderSettings)
    {
        *m_PostProcessingSettings = renderSettings.PostProcessing;
        activeScene->SetPhysicsBackend(static_cast<PhysicsBackend>(renderSettings.PhysicsBackend));

        // toRenderer=true 路径只从 settings 读取，不会修改
        SyncSSAOSettings(const_cast<EditorRenderSettings&>(renderSettings), true);

        if (renderSettings.MSAASamples != m_ViewportController->GetHDRFramebuffer()->GetSpecification().Samples)
            ApplyMSAASamples(renderSettings.MSAASamples);
        else
            SyncHDRFramebufferBindings();
    }

    EditorRenderSettings EditorRenderController::CollectRenderSettings(const Ref<Scene>& activeScene)
    {
        EditorRenderSettings renderSettings;
        renderSettings.PostProcessing = *m_PostProcessingSettings;
        renderSettings.MSAASamples = m_ViewportController->GetHDRFramebuffer()->GetSpecification().Samples;
        renderSettings.PhysicsBackend = static_cast<int>(activeScene->GetPhysicsBackend());
        SyncSSAOSettings(renderSettings, false);
        return renderSettings;
    }

    SceneRenderer& EditorRenderController::GetSceneRenderer() const
    {
        return *m_SceneRenderer;
    }

    void EditorRenderController::OnViewportResized(uint32_t width, uint32_t height)
    {
        m_SceneRenderer->ResizeHDR(width, height);
        SyncHDRFramebufferBindings();
    }

    void EditorRenderController::SyncHDRFramebufferBindings()
    {
        const auto& hdrFramebuffer = m_ViewportController->GetHDRFramebuffer();
        m_SceneRenderer->SetHDRFramebuffer(hdrFramebuffer);
        m_PanelCoordinator->SetHDRFramebuffer(hdrFramebuffer);
    }

} // namespace Engine
