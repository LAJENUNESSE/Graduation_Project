#include "EditorRenderController.h"

#include "Debug/PerformanceMonitor.h"
#include "Debug/ProfileTimer.h"
#include "EditorPanelCoordinator.h"
#include "EditorViewportController.h"
#include "Physics/PhysicsDebugDraw.h"
#include "Renderer/PostProcessing.h"
#include "Renderer/SceneRenderInput.h"
#include "Renderer/SceneRenderer.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"

#include <vector>

namespace Engine
{

    EditorRenderController::EditorRenderController(const Dependencies& dependencies)
        : m_SceneRenderer(dependencies.SceneRendererRef), m_PostProcessing(dependencies.PostProcessingRef),
          m_PostProcessingSettings(dependencies.PostProcessingSettingsRef),
          m_ViewportController(dependencies.ViewportController), m_PanelCoordinator(dependencies.PanelCoordinator),
          m_PhysicsDebugDraw(dependencies.PhysicsDebugDrawRef),
          m_ShowPhysicsColliders(dependencies.ShowPhysicsColliders),
          m_ShowMeshSDFBounds(dependencies.ShowMeshSDFBounds)
    {}

    void EditorRenderController::Attach()
    {
        const auto& viewportContext = m_ViewportController.GetContext();
        const uint32_t width = static_cast<uint32_t>(viewportContext.RenderSize.x);
        const uint32_t height = static_cast<uint32_t>(viewportContext.RenderSize.y);

        m_PostProcessing.Init(width, height);
        m_SceneRenderer.Init(width, height);
        m_SceneRenderer.SetPostProcessing(&m_PostProcessing, &m_PostProcessingSettings);
        m_ViewportController.SetResizeCallback(
            [this](uint32_t resizedWidth, uint32_t resizedHeight) { OnViewportResized(resizedWidth, resizedHeight); });
        m_SceneRenderer.SetDebugDrawCallback(
            [this]()
            {
                if (!m_ActiveScene)
                    return;

                if (!m_ShowPhysicsColliders && !m_ShowMeshSDFBounds)
                    return;

                if (m_ShowPhysicsColliders)
                    m_PhysicsDebugDraw.DrawColliders(m_ActiveScene->GetRegistry(), m_ViewportController.GetCamera());

                if (!m_ShowMeshSDFBounds)
                    return;

                std::vector<PhysicsDebugDraw::MeshSDFDebugBounds> bounds;
                auto fluidView = m_ActiveScene->GetRegistry().view<FluidEmitterComponent, TransformComponent>();
                for (auto entity : fluidView)
                {
                    auto& emitter = fluidView.get<FluidEmitterComponent>(entity);
                    if (!emitter.MeshSDFCoupling)
                        continue;

                    auto it = m_SceneRenderer.GetFluidSystems().find(static_cast<uint32_t>(entity));
                    if (it == m_SceneRenderer.GetFluidSystems().end() || !it->second)
                        continue;

                    for (const auto& body : it->second->GetMeshSDFDebugBodies())
                    {
                        PhysicsDebugDraw::MeshSDFDebugBounds b{};
                        b.Center = body.Center;
                        b.HalfExtents = body.HalfExtents;
                        b.Rotation = body.Rotation;
                        b.Color = glm::vec3(0.65f, 0.25f, 1.0f);
                        bounds.push_back(b);
                    }
                }

                if (!bounds.empty())
                    m_PhysicsDebugDraw.DrawMeshSDFBounds(bounds, m_ViewportController.GetCamera());
            });

        SyncHDRFramebufferBindings();
    }

    void EditorRenderController::Detach()
    {
        m_ViewportController.SetResizeCallback({});
        m_ActiveScene = nullptr;
        m_SceneRenderer.Shutdown();
    }

    void EditorRenderController::OnUpdate(Timestep ts, const Ref<Scene>& activeScene, SceneState sceneState)
    {
        if (!activeScene)
            return;

        m_ActiveScene = activeScene.get();

        switch (sceneState)
        {
        case SceneState::Edit:
            activeScene->OnUpdateEditor(ts, m_ViewportController.GetCamera());
            break;
        case SceneState::Play:
            activeScene->OnUpdateRuntime(ts, m_ViewportController.GetCamera());
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
            m_SceneRenderer.BeginScene(m_ViewportController.GetCamera(), input);
            m_SceneRenderer.RenderPipeline(m_ViewportController.GetFramebuffer());
            if (sceneState == SceneState::Edit)
                m_SceneRenderer.RenderEditorPicking(m_ViewportController.GetPickingFramebuffer());
            m_SceneRenderer.EndScene();
        }
        PerformanceMonitor::Get().SetSceneRenderCPU(sceneRenderCpuMs);
    }

    void EditorRenderController::ApplyMSAASamples(uint32_t samples)
    {
        m_ViewportController.ApplyMSAASamples(samples);
        SyncHDRFramebufferBindings();
    }

    void EditorRenderController::PushSSAOSettingsToRenderer(const EditorRenderSettings& settings) const
    {
        m_SceneRenderer.GetSSAOEnabled() = settings.SSAOEnabled;
        m_SceneRenderer.GetSSAORadius() = settings.SSAORadius;
        m_SceneRenderer.GetSSAOBias() = settings.SSAOBias;
        m_SceneRenderer.GetSSAOKernelSize() = settings.SSAOKernelSize;
        m_SceneRenderer.GetSSAOIntensity() = settings.SSAOIntensity;
    }

    void EditorRenderController::PullSSAOSettingsFromRenderer(EditorRenderSettings& settings) const
    {
        settings.SSAOEnabled = m_SceneRenderer.GetSSAOEnabled();
        settings.SSAORadius = m_SceneRenderer.GetSSAORadius();
        settings.SSAOBias = m_SceneRenderer.GetSSAOBias();
        settings.SSAOKernelSize = m_SceneRenderer.GetSSAOKernelSize();
        settings.SSAOIntensity = m_SceneRenderer.GetSSAOIntensity();
    }

    void EditorRenderController::ApplyRenderSettings(const Ref<Scene>& activeScene,
                                                     const EditorRenderSettings& renderSettings)
    {
        if (!activeScene)
            return;

        m_PostProcessingSettings = renderSettings.PostProcessing;
        PushSSAOSettingsToRenderer(renderSettings);

        if (renderSettings.MSAASamples != m_ViewportController.GetHDRFramebuffer()->GetSpecification().Samples)
            ApplyMSAASamples(renderSettings.MSAASamples);
        else
            SyncHDRFramebufferBindings();
    }

    EditorRenderSettings EditorRenderController::CollectRenderSettings(const Ref<Scene>& activeScene) const
    {
        EditorRenderSettings renderSettings;
        renderSettings.PostProcessing = m_PostProcessingSettings;
        renderSettings.MSAASamples = m_ViewportController.GetHDRFramebuffer()->GetSpecification().Samples;
        PullSSAOSettingsFromRenderer(renderSettings);
        return renderSettings;
    }

    SceneRenderer& EditorRenderController::GetSceneRenderer() const
    {
        return m_SceneRenderer;
    }

    void EditorRenderController::OnViewportResized(uint32_t width, uint32_t height)
    {
        m_SceneRenderer.ResizeHDR(width, height);
        SyncHDRFramebufferBindings();
    }

    void EditorRenderController::SyncHDRFramebufferBindings()
    {
        const auto& hdrFramebuffer = m_ViewportController.GetHDRFramebuffer();
        m_SceneRenderer.SetHDRFramebuffer(hdrFramebuffer);
        m_PanelCoordinator.SetHDRFramebuffer(hdrFramebuffer);
    }

} // namespace Engine
