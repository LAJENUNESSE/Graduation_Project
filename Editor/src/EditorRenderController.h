#pragma once

#include "Core/Timestep.h"
#include "EditorSceneSession.h"
#include "Scene/SceneSerializer.h"

namespace Engine
{

    class EditorPanelCoordinator;
    class EditorViewportController;
    class PhysicsDebugDraw;
    class PostProcessing;
    struct PostProcessingSettings;
    class Scene;
    class SceneRenderer;

    class EditorRenderController
    {
    public:
        struct Dependencies
        {
            SceneRenderer& SceneRendererRef;
            PostProcessing& PostProcessingRef;
            PostProcessingSettings& PostProcessingSettingsRef;
            EditorViewportController& ViewportController;
            EditorPanelCoordinator& PanelCoordinator;
            PhysicsDebugDraw& PhysicsDebugDrawRef;
            bool& ShowPhysicsColliders;
            bool& ShowMeshSDFBounds;
        };

        explicit EditorRenderController(const Dependencies& dependencies);

        void Attach();
        void Detach();
        void OnUpdate(Timestep ts, const Ref<Scene>& activeScene, SceneState sceneState);

        void ApplyMSAASamples(uint32_t samples);
        void ApplyRenderSettings(const Ref<Scene>& activeScene, const EditorRenderSettings& renderSettings);
        EditorRenderSettings CollectRenderSettings(const Ref<Scene>& activeScene) const;

        SceneRenderer& GetSceneRenderer() const;
        PostProcessingSettings* GetPostProcessingSettings() const { return &m_PostProcessingSettings; }

    private:
        void PushSSAOSettingsToRenderer(const EditorRenderSettings& settings) const;
        void PullSSAOSettingsFromRenderer(EditorRenderSettings& settings) const;
        void OnViewportResized(uint32_t width, uint32_t height);
        void SyncHDRFramebufferBindings();

    private:
        SceneRenderer& m_SceneRenderer;
        PostProcessing& m_PostProcessing;
        PostProcessingSettings& m_PostProcessingSettings;
        EditorViewportController& m_ViewportController;
        EditorPanelCoordinator& m_PanelCoordinator;
        PhysicsDebugDraw& m_PhysicsDebugDraw;
        bool& m_ShowPhysicsColliders;
        bool& m_ShowMeshSDFBounds;
        Scene* m_ActiveScene = nullptr;
    };

} // namespace Engine
