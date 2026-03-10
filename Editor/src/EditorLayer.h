#pragma once

#include "Engine.h"
#include "Renderer/PostProcessing.h"

namespace Engine
{
    class Scene;
    class SceneRenderer;
    class PhysicsDebugDraw;
    class SceneHierarchyPanel;
    class PropertiesPanel;
    class ConsolePanel;
    class AssetBrowserPanel;
    class RenderSettingsPanel;
    class CommandHistory;
    class EditorSceneSession;
    class EditorPanelCoordinator;
    class EditorSelectionGizmoController;
    class EditorShell;
    struct EditorShellActions;
    struct EditorShellState;
    class EditorViewportController;
    class EditorRenderController;

    class EditorLayer : public Layer
    {
    public:
        EditorLayer();
        ~EditorLayer() override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(Timestep ts) override;
        void OnImGuiRender() override;
        void OnEvent(Event& event) override;

    private:
        bool OnKeyPressed(KeyPressedEvent& e);
        bool OnMouseButtonPressed(MouseButtonPressedEvent& e);

        void NewScene();
        void OpenScene();
        void OpenScene(const std::string& filepath);
        void SaveScene();

        void OnScenePlay();
        void OnSceneStop();
        void HandleShellActions(const EditorShellActions& actions);
        EditorShellState BuildShellState() const;

        void BootstrapDefaultScene();
        void ConfigureEditorPanels();
        void ApplyActiveSceneContext(bool clearCommandHistory);

    private:
        Ref<Scene> m_ActiveScene;
        Scope<EditorSceneSession> m_SceneSession;
        Scope<EditorPanelCoordinator> m_PanelCoordinator;
        Scope<EditorSelectionGizmoController> m_SelectionGizmoController;
        Scope<EditorShell> m_EditorShell;
        Scope<EditorViewportController> m_ViewportController;
        Scope<EditorRenderController> m_RenderController;

        Scope<SceneRenderer> m_SceneRenderer;
        Scope<PostProcessing> m_PostProcessing;
        PostProcessingSettings m_PostProcessingSettings;

        Scope<SceneHierarchyPanel> m_HierarchyPanel;
        Scope<PropertiesPanel> m_PropertiesPanel;
        Scope<ConsolePanel> m_ConsolePanel;
        Scope<AssetBrowserPanel> m_AssetBrowserPanel;
        Scope<RenderSettingsPanel> m_RenderSettingsPanel;

        Scope<CommandHistory> m_CommandHistory;

        bool m_ShowPhysicsColliders = false;
        Scope<PhysicsDebugDraw> m_PhysicsDebugDraw;
    };

} // namespace Engine
