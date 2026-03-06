#pragma once

#include "Engine.h"
#include "Scene/Scene.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Panels/PropertiesPanel.h"
#include "Panels/ConsolePanel.h"
#include "Panels/AssetBrowserPanel.h"
#include "Panels/RenderSettingsPanel.h"
#include "UndoSystem.h"
#include "Renderer/PostProcessing.h"
#include "Renderer/SceneRenderer.h"
#include "Physics/PhysicsDebugDraw.h"
#include "EditorRenderController.h"
#include "EditorSceneSession.h"
#include "EditorPanelCoordinator.h"
#include "EditorSelectionGizmoController.h"
#include "EditorShell.h"
#include "EditorViewportController.h"

namespace Engine
{

    class EditorLayer : public Layer
    {
    public:
        EditorLayer();
        ~EditorLayer() override = default;

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
        EditorSceneSession m_SceneSession;
        EditorPanelCoordinator m_PanelCoordinator;
        EditorSelectionGizmoController m_SelectionGizmoController;
        EditorShell m_EditorShell;
        EditorViewportController m_ViewportController;
        EditorRenderController m_RenderController;

        SceneRenderer m_SceneRenderer;
        PostProcessing m_PostProcessing;
        PostProcessingSettings m_PostProcessingSettings;

        SceneHierarchyPanel m_HierarchyPanel;
        PropertiesPanel m_PropertiesPanel;
        ConsolePanel m_ConsolePanel;
        AssetBrowserPanel m_AssetBrowserPanel;
        RenderSettingsPanel m_RenderSettingsPanel;

        CommandHistory m_CommandHistory;

        bool m_ShowPhysicsColliders = false;
        PhysicsDebugDraw m_PhysicsDebugDraw;
    };

} // namespace Engine
