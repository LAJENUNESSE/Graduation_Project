#pragma once

#include "Core/Base.h"
#include "Events/KeyEvent.h"
#include "Events/MouseEvent.h"
#include "Renderer/EditorCamera.h"
#include "Renderer/Framebuffer.h"
#include "Scene/Entity.h"

#include <glm/glm.hpp>
#include <vector>

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
        void OnMouseButtonPressed(const MouseButtonPressedEvent& e,
                                  const EditorViewportContext&   viewport,
                                  Ref<Scene>                     activeScene);
        void UpdateHoveredEntity(const EditorViewportContext& viewport,
                                 const Ref<Framebuffer>&      pickingFramebuffer,
                                 const Ref<Scene>&            activeScene);
        void RenderGizmos(const EditorViewportContext& viewport, EditorCamera& camera, const Ref<Scene>& activeScene);
        void ClearTransientState();

    private:
        struct GizmoTransformSnapshot
        {
            UUID      EntityID    = 0;
            glm::vec3 Translation = {};
            glm::vec3 Rotation    = {};
            glm::vec3 Scale       = {1.0f, 1.0f, 1.0f};
        };

        void CaptureGizmoSelectionStartStates(const Ref<Scene>&          activeScene,
                                              const std::vector<Entity>& selectedEntities);

        EditorPanelCoordinator* m_PanelCoordinator = nullptr;
        CommandHistory*         m_CommandHistory   = nullptr;

        Entity                              m_HoveredEntity;
        int                                 m_GizmoType             = -1;
        bool                                m_GizmoWasUsing         = false;
        glm::vec3                           m_GizmoStartTranslation = {};
        glm::vec3                           m_GizmoStartRotation    = {};
        glm::vec3                           m_GizmoStartScale       = {};
        std::vector<GizmoTransformSnapshot> m_GizmoSelectionStartStates;
    };

} // namespace Engine