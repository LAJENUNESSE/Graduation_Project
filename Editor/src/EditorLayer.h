#pragma once

#include "Engine.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Panels/PropertiesPanel.h"

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
        void SaveScene();

    private:
        Ref<Framebuffer> m_Framebuffer;
        Ref<Scene> m_ActiveScene;

        EditorCamera m_EditorCamera;
        Entity m_SelectedEntity;
        Entity m_HoveredEntity;

        SceneHierarchyPanel m_HierarchyPanel;
        PropertiesPanel m_PropertiesPanel;

        glm::vec2 m_ViewportSize = {1280.0f, 720.0f};
        glm::vec2 m_ViewportBounds[2] = {};

        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;
        glm::vec2 m_LastMousePos = {0.0f, 0.0f};

        int m_GizmoType = -1; // -1 = no gizmo

        // Performance panel
        bool m_ShowStatsPanel = true;
    };

} // namespace Engine
