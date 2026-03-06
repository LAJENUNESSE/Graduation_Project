#include "EditorRenderController.h"

#include "Debug/PerformanceMonitor.h"
#include "Debug/ProfileTimer.h"
#include "EditorPanelCoordinator.h"
#include "EditorViewportController.h"
#include "Physics/PhysicsDebugDraw.h"
#include "Renderer/PostProcessing.h"
#include "Renderer/SceneRenderer.h"
#include "Scene/Scene.h"

namespace Engine
{

    void EditorRenderController::Initialize(SceneRenderer* sceneRenderer,
                                            PostProcessing* postProcessing,
                                            PostProcessingSettings* postProcessingSettings,
                                            EditorViewportController* viewportController,
                                            EditorPanelCoordinator* panelCoordinator,
                                            PhysicsDebugDraw* physicsDebugDraw,
                                            bool* showPhysicsColliders)
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
        m_ViewportController->SetResizeCallback([this](uint32_t resizedWidth, uint32_t resizedHeight) {
            OnViewportResized(resizedWidth, resizedHeight);
        });
        m_SceneRenderer->SetDebugDrawCallback([this]() {
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
            m_SceneRenderer->BeginScene(m_ViewportController->GetCamera(), activeScene.get(), ts);
            m_SceneRenderer->RenderPipeline(m_ViewportController->GetFramebuffer());
            m_SceneRenderer->EndScene();
        }
        PerformanceMonitor::Get().SetSceneRenderCPU(sceneRenderCpuMs);
    }

    void EditorRenderController::ApplyMSAASamples(uint32_t samples)
    {
        m_ViewportController->ApplyMSAASamples(samples);
        SyncHDRFramebufferBindings();
    }

    void EditorRenderController::ApplyRenderSettings(const Ref<Scene>& activeScene,
                                                     const EditorRenderSettings& renderSettings)
    {
        *m_PostProcessingSettings = renderSettings.PostProcessing;
        activeScene->SetPhysicsBackend(static_cast<PhysicsBackend>(renderSettings.PhysicsBackend));

        m_SceneRenderer->GetSSAOEnabled() = renderSettings.SSAOEnabled;
        m_SceneRenderer->GetSSAORadius() = renderSettings.SSAORadius;
        m_SceneRenderer->GetSSAOBias() = renderSettings.SSAOBias;
        m_SceneRenderer->GetSSAOKernelSize() = renderSettings.SSAOKernelSize;
        m_SceneRenderer->GetSSAOIntensity() = renderSettings.SSAOIntensity;

        if (renderSettings.MSAASamples != m_ViewportController->GetHDRFramebuffer()->GetSpecification().Samples)
            ApplyMSAASamples(renderSettings.MSAASamples);
        else
            SyncHDRFramebufferBindings();
    }

    EditorRenderSettings EditorRenderController::CollectRenderSettings(const Ref<Scene>& activeScene) const
    {
        EditorRenderSettings renderSettings;
        renderSettings.PostProcessing = *m_PostProcessingSettings;
        renderSettings.MSAASamples = m_ViewportController->GetHDRFramebuffer()->GetSpecification().Samples;
        renderSettings.PhysicsBackend = static_cast<int>(activeScene->GetPhysicsBackend());
        renderSettings.SSAOEnabled = m_SceneRenderer->GetSSAOEnabled();
        renderSettings.SSAORadius = m_SceneRenderer->GetSSAORadius();
        renderSettings.SSAOBias = m_SceneRenderer->GetSSAOBias();
        renderSettings.SSAOKernelSize = m_SceneRenderer->GetSSAOKernelSize();
        renderSettings.SSAOIntensity = m_SceneRenderer->GetSSAOIntensity();
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
