#include "EditorSelectionGizmoController.h"

#include "Core/Input.h"
#include "Core/KeyCodes.h"
#include "Core/MouseCodes.h"
#include "EditorPanelCoordinator.h"
#include "EditorViewportController.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "UndoSystem.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>

namespace Engine
{

    void EditorSelectionGizmoController::Initialize(EditorPanelCoordinator* panelCoordinator,
                                                    CommandHistory* commandHistory)
    {
        m_PanelCoordinator = panelCoordinator;
        m_CommandHistory = commandHistory;
    }

    void EditorSelectionGizmoController::OnKeyPressed(const KeyPressedEvent& e)
    {
        if (e.GetRepeatCount() > 0)
            return;

        switch (static_cast<KeyCode>(e.GetKeyCode()))
        {
        case KeyCode::Q:
            if (!ImGuizmo::IsUsing())
                m_GizmoType = -1;
            break;
        case KeyCode::W:
            if (!ImGuizmo::IsUsing())
                m_GizmoType = ImGuizmo::TRANSLATE;
            break;
        case KeyCode::E:
            if (!ImGuizmo::IsUsing())
                m_GizmoType = ImGuizmo::ROTATE;
            break;
        case KeyCode::R:
            if (!ImGuizmo::IsUsing())
                m_GizmoType = ImGuizmo::SCALE;
            break;
        default:
            break;
        }
    }

    void EditorSelectionGizmoController::OnMouseButtonPressed(const MouseButtonPressedEvent& e,
                                                              const EditorViewportContext& viewport,
                                                              Ref<Scene> activeScene)
    {
        (void)activeScene;

        if (e.GetMouseButton() != static_cast<int>(MouseCode::ButtonLeft))
            return;

        if (viewport.Hovered && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing() &&
            !Input::IsKeyPressed(KeyCode::LeftAlt) && m_PanelCoordinator)
        {
            m_PanelCoordinator->SetPrimarySelection(m_HoveredEntity);
        }
    }

    void EditorSelectionGizmoController::UpdateHoveredEntity(const EditorViewportContext& viewport,
                                                             const Ref<Framebuffer>& pickingFramebuffer,
                                                             const Ref<Scene>& activeScene)
    {
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            return;

        ImVec2 mousePos = ImGui::GetMousePos();
        float mx = mousePos.x - viewport.Bounds[0].x;
        float my = mousePos.y - viewport.Bounds[0].y;
        glm::vec2 displaySize = viewport.Bounds[1] - viewport.Bounds[0];
        if (displaySize.x <= 0.0f || displaySize.y <= 0.0f)
            return;

        const auto pickingSpec = pickingFramebuffer->GetSpecification();
        float scaleX = static_cast<float>(pickingSpec.Width) / displaySize.x;
        float scaleY = static_cast<float>(pickingSpec.Height) / displaySize.y;
        int mouseX = static_cast<int>(mx * scaleX);
        int mouseY = static_cast<int>((displaySize.y - my) * scaleY);
        if (mouseX < 0 || mouseY < 0 ||
            mouseX >= static_cast<int>(pickingSpec.Width) || mouseY >= static_cast<int>(pickingSpec.Height))
            return;

        int pixelData = pickingFramebuffer->ReadPixel(1, mouseX, mouseY);
        m_HoveredEntity =
            pixelData == -1 ? Entity() : Entity(static_cast<entt::entity>(pixelData), activeScene.get());
    }

    void EditorSelectionGizmoController::RenderGizmos(const EditorViewportContext& viewport,
                                                      EditorCamera& camera,
                                                      const Ref<Scene>& activeScene)
    {
        if (!m_PanelCoordinator || !activeScene)
            return;

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(viewport.Bounds[0].x, viewport.Bounds[0].y,
                          viewport.Bounds[1].x - viewport.Bounds[0].x,
                          viewport.Bounds[1].y - viewport.Bounds[0].y);

        Entity selectedEntity = m_PanelCoordinator->GetPrimarySelection();
        const auto& selectedEntities = m_PanelCoordinator->GetSelectedEntities();
        if (selectedEntity && m_GizmoType != -1 && selectedEntity.HasComponent<TransformComponent>())
        {
            const glm::mat4& cameraView = camera.GetViewMatrix();
            const glm::mat4& cameraProjection = camera.GetProjection();

            auto& tc = selectedEntity.GetComponent<TransformComponent>();
            glm::mat4 worldTransform = activeScene->GetWorldTransform(selectedEntity);

            glm::mat4 parentWorldTransform(1.0f);
            if (selectedEntity.HasComponent<RelationshipComponent>())
            {
                auto& rel = selectedEntity.GetComponent<RelationshipComponent>();
                if (static_cast<uint64_t>(rel.ParentID) != 0)
                {
                    Entity parent = activeScene->FindEntityByUUID(rel.ParentID);
                    if (parent)
                        parentWorldTransform = activeScene->GetWorldTransform(parent);
                }
            }

            glm::vec3 prevWorldPos = glm::vec3(worldTransform[3]);
            glm::mat4 transform = worldTransform;

            ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
                                 static_cast<ImGuizmo::OPERATION>(m_GizmoType), ImGuizmo::LOCAL,
                                 glm::value_ptr(transform));

            if (ImGuizmo::IsUsing())
            {
                if (!m_GizmoWasUsing)
                {
                    m_GizmoStartTranslation = tc.Translation;
                    m_GizmoStartRotation = tc.Rotation;
                    m_GizmoStartScale = tc.Scale;
                    CaptureGizmoSelectionStartStates(activeScene, selectedEntities);
                    m_GizmoWasUsing = true;
                }

                glm::mat4 localTransform = glm::inverse(parentWorldTransform) * transform;

                glm::vec3 translation, rotation, scale;
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(localTransform), glm::value_ptr(translation),
                                                      glm::value_ptr(rotation), glm::value_ptr(scale));

                glm::vec3 deltaRotation = glm::radians(rotation) - tc.Rotation;
                tc.Translation = translation;
                tc.Rotation += deltaRotation;
                tc.Scale = scale;

                if (selectedEntities.size() > 1 && m_GizmoType == ImGuizmo::TRANSLATE)
                {
                    glm::vec3 newWorldPos = glm::vec3(transform[3]);
                    glm::vec3 worldDelta = newWorldPos - prevWorldPos;

                    for (const auto& otherEntity : selectedEntities)
                    {
                        if (otherEntity == selectedEntity || !otherEntity)
                            continue;

                        auto handle = static_cast<entt::entity>(otherEntity);
                        auto& reg = activeScene->GetRegistry();
                        if (!reg.all_of<TransformComponent>(handle))
                            continue;

                        auto& otherTc = reg.get<TransformComponent>(handle);
                        glm::mat4 otherParentWorld(1.0f);
                        if (reg.all_of<RelationshipComponent>(handle))
                        {
                            auto& otherRel = reg.get<RelationshipComponent>(handle);
                            if (static_cast<uint64_t>(otherRel.ParentID) != 0)
                            {
                                Entity otherParent = activeScene->FindEntityByUUID(otherRel.ParentID);
                                if (otherParent)
                                    otherParentWorld = activeScene->GetWorldTransform(otherParent);
                            }
                        }

                        glm::vec3 localDelta = glm::vec3(glm::inverse(otherParentWorld) * glm::vec4(worldDelta, 0.0f));
                        otherTc.Translation += localDelta;
                    }
                }
            }
            else if (m_GizmoWasUsing && m_CommandHistory)
            {
                m_GizmoWasUsing = false;
                if (m_GizmoSelectionStartStates.size() > 1)
                {
                    std::vector<MultiTransformChangeCommand::Entry> entries;
                    entries.reserve(m_GizmoSelectionStartStates.size());

                    for (const auto& snapshot : m_GizmoSelectionStartStates)
                    {
                        Entity entity = activeScene->FindEntityByUUID(snapshot.EntityID);
                        if (!entity || !entity.HasComponent<TransformComponent>())
                            continue;

                        const auto& current = entity.GetComponent<TransformComponent>();
                        entries.push_back({
                            snapshot.EntityID,
                            snapshot.Translation,
                            snapshot.Rotation,
                            snapshot.Scale,
                            current.Translation,
                            current.Rotation,
                            current.Scale
                        });
                    }

                    if (!entries.empty())
                        m_CommandHistory->PushCommand(
                            CreateRef<MultiTransformChangeCommand>(activeScene, std::move(entries)));
                }
                else
                {
                    auto cmd = CreateRef<TransformChangeCommand>(
                        selectedEntity,
                        m_GizmoStartTranslation, m_GizmoStartRotation, m_GizmoStartScale,
                        tc.Translation, tc.Rotation, tc.Scale);
                    m_CommandHistory->PushCommand(cmd);
                }

                m_GizmoSelectionStartStates.clear();
            }
        }

        glm::mat4 viewMatrix = camera.GetViewMatrix();
        float viewManipulateRight = viewport.Bounds[1].x;
        float viewManipulateTop = viewport.Bounds[0].y;
        ImGuizmo::ViewManipulate(glm::value_ptr(viewMatrix), camera.GetDistance(),
                                 ImVec2(viewManipulateRight - 128, viewManipulateTop), ImVec2(128, 128),
                                 0x10101010);
        if (viewMatrix != camera.GetViewMatrix())
            camera.SetViewMatrix(viewMatrix);
    }

    void EditorSelectionGizmoController::ClearTransientState()
    {
        m_HoveredEntity = {};
        m_GizmoWasUsing = false;
        m_GizmoSelectionStartStates.clear();
    }

    void EditorSelectionGizmoController::CaptureGizmoSelectionStartStates(
        const Ref<Scene>& activeScene, const std::vector<Entity>& selectedEntities)
    {
        m_GizmoSelectionStartStates.clear();
        if (!activeScene)
            return;

        m_GizmoSelectionStartStates.reserve(selectedEntities.size());
        for (Entity entity : selectedEntities)
        {
            if (!entity || entity.GetScene() != activeScene.get() || !entity.HasComponent<TransformComponent>())
                continue;

            const auto& tc = entity.GetComponent<TransformComponent>();
            m_GizmoSelectionStartStates.push_back({
                entity.GetUUID(),
                tc.Translation,
                tc.Rotation,
                tc.Scale
            });
        }
    }

} // namespace Engine
