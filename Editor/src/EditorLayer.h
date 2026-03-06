#pragma once

#include "Engine.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Panels/PropertiesPanel.h"
#include "Panels/ConsolePanel.h"
#include "Panels/AssetBrowserPanel.h"
#include "Panels/RenderSettingsPanel.h"
#include "UndoSystem.h"
#include "Renderer/PostProcessing.h"
#include "Renderer/SceneRenderer.h"
#include "Physics/PhysicsDebugDraw.h"
#include "EditorSceneSession.h"
#include "EditorPanelCoordinator.h"
#include "EditorShell.h"

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

    private:
        Ref<Framebuffer> m_Framebuffer;
        Ref<Framebuffer> m_HDRFramebuffer;
        Ref<Scene> m_ActiveScene;
        EditorSceneSession m_SceneSession;
        EditorPanelCoordinator m_PanelCoordinator;
        EditorShell m_EditorShell;

        SceneRenderer m_SceneRenderer;
        PostProcessing m_PostProcessing;
        PostProcessingSettings m_PostProcessingSettings;

        EditorCamera m_EditorCamera;
        Entity m_SelectedEntity;
        Entity m_HoveredEntity;

        SceneHierarchyPanel m_HierarchyPanel;
        PropertiesPanel m_PropertiesPanel;
        ConsolePanel m_ConsolePanel;
        AssetBrowserPanel m_AssetBrowserPanel;
        RenderSettingsPanel m_RenderSettingsPanel;

        // Undo/Redo 系统
        CommandHistory m_CommandHistory;

        // Gizmo 拖拽 Transform 快照（用于生成 undo 命令）
        bool m_GizmoWasUsing = false;
        glm::vec3 m_GizmoStartTranslation = {};
        glm::vec3 m_GizmoStartRotation = {};
        glm::vec3 m_GizmoStartScale = {};

        glm::vec2 m_ViewportSize = {1280.0f, 720.0f};
        glm::vec2 m_ViewportBounds[2] = {};

        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;
        glm::vec2 m_LastMousePos = {0.0f, 0.0f};

        int m_GizmoType = -1;

        bool m_ShowPhysicsColliders = false;
        PhysicsDebugDraw m_PhysicsDebugDraw;
    };

} // namespace Engine
