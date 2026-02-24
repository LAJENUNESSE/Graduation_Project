#pragma once

#include "Engine.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Panels/PropertiesPanel.h"
#include "Renderer/PostProcessing.h"
#include "Renderer/SceneRenderer.h"
#include "Physics/PhysicsDebugDraw.h"

namespace Engine
{

    enum class SceneState { Edit = 0, Play = 1 };

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

        void OnScenePlay();
        void OnSceneStop();

    private:
        Ref<Framebuffer> m_Framebuffer;
        Ref<Framebuffer> m_HDRFramebuffer;
        Ref<Scene> m_ActiveScene;
        Ref<Scene> m_EditorScene;

        SceneState m_SceneState = SceneState::Edit;

        SceneRenderer m_SceneRenderer;
        PostProcessing m_PostProcessing;
        PostProcessingSettings m_PostProcessingSettings;

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

        int m_GizmoType = -1;

        bool m_ShowStatsPanel = true;

        bool m_ShowPhysicsColliders = false;
        PhysicsDebugDraw m_PhysicsDebugDraw;
    };

} // namespace Engine
