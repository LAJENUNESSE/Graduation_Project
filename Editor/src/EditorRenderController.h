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
        void Initialize(SceneRenderer* sceneRenderer, PostProcessing* postProcessing,
                        PostProcessingSettings* postProcessingSettings, EditorViewportController* viewportController,
                        EditorPanelCoordinator* panelCoordinator, PhysicsDebugDraw* physicsDebugDraw,
                        bool* showPhysicsColliders);

        void Attach();
        void Detach();
        void OnUpdate(Timestep ts, const Ref<Scene>& activeScene, SceneState sceneState);

        void ApplyMSAASamples(uint32_t samples);
        void ApplyRenderSettings(const Ref<Scene>& activeScene, const EditorRenderSettings& renderSettings);
        EditorRenderSettings CollectRenderSettings(const Ref<Scene>& activeScene);

        SceneRenderer& GetSceneRenderer() const;
        PostProcessingSettings* GetPostProcessingSettings() const { return m_PostProcessingSettings; }

    private:
        // direction: true = settings→renderer, false = renderer→settings
        void SyncSSAOSettings(EditorRenderSettings& settings, bool toRenderer);
        void OnViewportResized(uint32_t width, uint32_t height);
        void SyncHDRFramebufferBindings();

    private:
        SceneRenderer* m_SceneRenderer = nullptr;
        PostProcessing* m_PostProcessing = nullptr;
        PostProcessingSettings* m_PostProcessingSettings = nullptr;
        EditorViewportController* m_ViewportController = nullptr;
        EditorPanelCoordinator* m_PanelCoordinator = nullptr;
        PhysicsDebugDraw* m_PhysicsDebugDraw = nullptr;
        bool* m_ShowPhysicsColliders = nullptr;
        Scene* m_ActiveScene = nullptr;
    };

} // namespace Engine
