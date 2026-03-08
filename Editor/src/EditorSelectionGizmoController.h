#pragma once

#include "Core/Base.h"
#include "Events/KeyEvent.h"
#include "Events/MouseEvent.h"
#include "Renderer/EditorCamera.h"
#include "Renderer/Framebuffer.h"
#include "Scene/Entity.h"

#include <glm/glm.hpp>

namespace Engine
{

    class Scene;
    class CommandHistory;
    class EditorPanelCoordinator;
    struct EditorViewportContext;

    class EditorSelectionGizmoController
    {
    public:
        void Initialize(EditorPanelCoordinator* panelCoordinator, CommandHistory* commandHistory);

        void OnKeyPressed(const KeyPressedEvent& e);
        void OnMouseButtonPressed(const MouseButtonPressedEvent& e, const EditorViewportContext& viewport,
                                  Ref<Scene> activeScene);
        void UpdateHoveredEntity(const EditorViewportContext& viewport, const Ref<Framebuffer>& pickingFramebuffer,
                                 const Ref<Scene>& activeScene);
        void RenderGizmos(const EditorViewportContext& viewport, EditorCamera& camera, const Ref<Scene>& activeScene);
        void ClearTransientState();

    private:
        EditorPanelCoordinator* m_PanelCoordinator = nullptr;
        CommandHistory* m_CommandHistory = nullptr;

        Entity m_HoveredEntity;
        int m_GizmoType = -1;
        bool m_GizmoWasUsing = false;
        glm::vec3 m_GizmoStartTranslation = {};
        glm::vec3 m_GizmoStartRotation = {};
        glm::vec3 m_GizmoStartScale = {};
    };

} // namespace Engine
