#include "EditorLayer.h"

#include "Asset/AssetManager.h"
#include "Core/FileDialogs.h"
#include "Core/Input.h"
#include "Core/Log.h"
#include "Debug/PerformanceMonitor.h"
#include "Debug/ProfileTimer.h"
#include "Renderer/Mesh.h"
#include "Renderer/Renderer.h"
#include "Scene/Components.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

namespace Engine
{

    EditorLayer::EditorLayer()
        : Layer("EditorLayer")
    {
    }

    void EditorLayer::OnAttach()
    {
        ENGINE_INFO("EditorLayer OnAttach");

        m_ViewportController.Initialize();
        const auto& viewportContext = m_ViewportController.GetContext();

        m_PostProcessing.Init(static_cast<uint32_t>(viewportContext.Size.x),
                              static_cast<uint32_t>(viewportContext.Size.y));

        m_SceneRenderer.Init(static_cast<uint32_t>(viewportContext.Size.x),
                             static_cast<uint32_t>(viewportContext.Size.y));
        m_SceneRenderer.SetHDRFramebuffer(m_ViewportController.GetHDRFramebuffer());
        m_SceneRenderer.SetPostProcessing(&m_PostProcessing, &m_PostProcessingSettings);
        m_SceneSession.Initialize(&m_SceneRenderer);
        m_SceneRenderer.SetDebugDrawCallback([this]() {
            if (m_ShowPhysicsColliders)
                m_PhysicsDebugDraw.DrawColliders(m_ActiveScene->GetRegistry(), m_ViewportController.GetCamera());
        });

        m_ActiveScene = CreateRef<Scene>();
        m_ActiveScene->SetSceneRenderer(&m_SceneRenderer);

        Entity cubeEntity = m_ActiveScene->CreateEntity("Cube");
        auto& meshRenderer = cubeEntity.AddComponent<MeshRendererComponent>();
        meshRenderer.Type = MeshType::Cube;
        meshRenderer.MeshAsset = AssetManager::Load<Mesh>("builtin:Cube");
        meshRenderer.Color = {0.8f, 0.2f, 0.3f, 1.0f};

        Entity lightEntity = m_ActiveScene->CreateEntity("方向光");
        auto& light = lightEntity.AddComponent<LightComponent>();
        light.Type = LightComponent::LightType::Directional;
        light.Color = {1.0f, 0.95f, 0.9f};
        auto& lightTransform = lightEntity.GetComponent<TransformComponent>();
        lightTransform.Rotation = {glm::radians(-45.0f), glm::radians(30.0f), 0.0f};

        m_HierarchyPanel.SetCommandHistory(&m_CommandHistory);
        m_AssetBrowserPanel.SetSceneOpenCallback([this](const std::string& path) {
            OpenScene(path);
        });

        m_RenderSettingsPanel.SetContext(&m_SceneRenderer, &m_PostProcessingSettings,
                                         m_ViewportController.GetHDRFramebuffer(), m_ActiveScene,
                                         &m_ShowPhysicsColliders);
        m_RenderSettingsPanel.SetMSAAChangedCallback([this](uint32_t samples) {
            m_ViewportController.ApplyMSAASamples(samples);
            m_SceneRenderer.SetHDRFramebuffer(m_ViewportController.GetHDRFramebuffer());
            m_PanelCoordinator.SetHDRFramebuffer(m_ViewportController.GetHDRFramebuffer());
        });

        m_PanelCoordinator.Initialize(
            &m_HierarchyPanel,
            &m_PropertiesPanel,
            &m_ConsolePanel,
            &m_AssetBrowserPanel,
            &m_RenderSettingsPanel,
            &m_SelectedEntity,
            &m_HoveredEntity,
            &m_CommandHistory);
        m_PanelCoordinator.ApplyScene(m_ActiveScene, false);

        m_ConsolePanel.RegisterSink();
    }

    void EditorLayer::OnDetach()
    {
        ENGINE_INFO("[EditorEvent] Detaching editor layer");
        if (m_SceneSession.IsPlaying())
            OnSceneStop();
        m_ConsolePanel.UnregisterSink();
        m_SceneRenderer.Shutdown();
    }

    void EditorLayer::OnUpdate(Timestep ts)
    {
        m_ViewportController.OnUpdate(ts, *m_ActiveScene);

        AssetManager::Update(ts);

        switch (m_SceneSession.GetState())
        {
        case SceneState::Edit:
            m_ActiveScene->OnUpdateEditor(ts, m_ViewportController.GetCamera());
            break;
        case SceneState::Play:
            m_ActiveScene->OnUpdateRuntime(ts, m_ViewportController.GetCamera());
            break;
        }

        float sceneRenderCpuMs = 0.0f;
        {
            PROFILE_SCOPE("SceneRender", &sceneRenderCpuMs);
            m_SceneRenderer.BeginScene(m_ViewportController.GetCamera(), m_ActiveScene.get(), ts);
            m_SceneRenderer.RenderPipeline(m_ViewportController.GetFramebuffer());
            m_SceneRenderer.EndScene();
        }
        PerformanceMonitor::Get().SetSceneRenderCPU(sceneRenderCpuMs);
    }

    void EditorLayer::OnImGuiRender()
    {
        HandleShellActions(m_EditorShell.Draw(BuildShellState()));

        m_PanelCoordinator.RenderPanels();

        EditorViewportContext viewportContext = m_ViewportController.BeginViewportWindow();

        // Mouse picking: only read back on click to avoid a per-frame GPU stall.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            ImVec2 mousePos = ImGui::GetMousePos();
            float mx = mousePos.x - viewportContext.Bounds[0].x;
            float my = mousePos.y - viewportContext.Bounds[0].y;
            glm::vec2 vpSize = viewportContext.Bounds[1] - viewportContext.Bounds[0];
            my = vpSize.y - my;

            int mouseX = static_cast<int>(mx);
            int mouseY = static_cast<int>(my);
            if (mouseX >= 0 && mouseY >= 0 && mouseX < static_cast<int>(vpSize.x) &&
                mouseY < static_cast<int>(vpSize.y))
            {
                int pixelData = m_ViewportController.GetHDRFramebuffer()->ReadPixel(1, mouseX, mouseY);
                m_HoveredEntity =
                    pixelData == -1 ? Entity() : Entity(static_cast<entt::entity>(pixelData), m_ActiveScene.get());
            }
        }

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(viewportContext.Bounds[0].x, viewportContext.Bounds[0].y,
                          viewportContext.Bounds[1].x - viewportContext.Bounds[0].x,
                          viewportContext.Bounds[1].y - viewportContext.Bounds[0].y);

        auto& selectedEntities = m_HierarchyPanel.GetSelectedEntities();
        if (m_SelectedEntity && m_GizmoType != -1 && m_SelectedEntity.HasComponent<TransformComponent>())
        {
            const glm::mat4& cameraView = m_ViewportController.GetCamera().GetViewMatrix();
            const glm::mat4& cameraProjection = m_ViewportController.GetCamera().GetProjection();

            auto& tc = m_SelectedEntity.GetComponent<TransformComponent>();
            glm::mat4 worldTransform = m_ActiveScene->GetWorldTransform(m_SelectedEntity);

            glm::mat4 parentWorldTransform(1.0f);
            if (m_SelectedEntity.HasComponent<RelationshipComponent>())
            {
                auto& rel = m_SelectedEntity.GetComponent<RelationshipComponent>();
                if (static_cast<uint64_t>(rel.ParentID) != 0)
                {
                    Entity parent = m_ActiveScene->FindEntityByUUID(rel.ParentID);
                    if (parent)
                        parentWorldTransform = m_ActiveScene->GetWorldTransform(parent);
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
                        if (otherEntity == m_SelectedEntity || !otherEntity)
                            continue;

                        auto handle = static_cast<entt::entity>(otherEntity);
                        auto& reg = m_ActiveScene->GetRegistry();
                        if (!reg.all_of<TransformComponent>(handle))
                            continue;

                        auto& otherTc = reg.get<TransformComponent>(handle);

                        glm::mat4 otherParentWorld(1.0f);
                        if (reg.all_of<RelationshipComponent>(handle))
                        {
                            auto& otherRel = reg.get<RelationshipComponent>(handle);
                            if (static_cast<uint64_t>(otherRel.ParentID) != 0)
                            {
                                Entity otherParent = m_ActiveScene->FindEntityByUUID(otherRel.ParentID);
                                if (otherParent)
                                    otherParentWorld = m_ActiveScene->GetWorldTransform(otherParent);
                            }
                        }

                        glm::vec3 localDelta = glm::vec3(glm::inverse(otherParentWorld) * glm::vec4(worldDelta, 0.0f));
                        otherTc.Translation += localDelta;
                    }
                }
            }
            else if (m_GizmoWasUsing)
            {
                m_GizmoWasUsing = false;
                auto cmd = CreateRef<TransformChangeCommand>(
                    m_SelectedEntity,
                    m_GizmoStartTranslation, m_GizmoStartRotation, m_GizmoStartScale,
                    tc.Translation, tc.Rotation, tc.Scale);
                m_CommandHistory.PushCommand(cmd);
            }
        }

        glm::mat4 viewMatrix = m_ViewportController.GetCamera().GetViewMatrix();
        float viewManipulateRight = viewportContext.Bounds[1].x;
        float viewManipulateTop = viewportContext.Bounds[0].y;
        ImGuizmo::ViewManipulate(glm::value_ptr(viewMatrix), m_ViewportController.GetCamera().GetDistance(),
                                 ImVec2(viewManipulateRight - 128, viewManipulateTop), ImVec2(128, 128),
                                 0x10101010);
        if (viewMatrix != m_ViewportController.GetCamera().GetViewMatrix())
            m_ViewportController.GetCamera().SetViewMatrix(viewMatrix);

        m_ViewportController.EndViewportWindow();
    }

    void EditorLayer::OnEvent(Event& event)
    {
        m_ViewportController.OnEvent(event);

        EventDispatcher dispatcher(event);
        dispatcher.Dispatch<KeyPressedEvent>(ENGINE_BIND_EVENT_FN(EditorLayer::OnKeyPressed));
        dispatcher.Dispatch<MouseButtonPressedEvent>(ENGINE_BIND_EVENT_FN(EditorLayer::OnMouseButtonPressed));
    }

    bool EditorLayer::OnKeyPressed(KeyPressedEvent& e)
    {
        HandleShellActions(m_EditorShell.OnKeyPressed(e, BuildShellState()));

        if (e.GetRepeatCount() > 0)
            return false;

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

        return false;
    }

    bool EditorLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        if (e.GetMouseButton() == static_cast<int>(MouseCode::ButtonLeft))
        {
            if (m_ViewportController.GetContext().Hovered && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing() &&
                !Input::IsKeyPressed(KeyCode::LeftAlt))
            {
                m_HierarchyPanel.SetSelectedEntity(m_HoveredEntity);
            }
        }
        return false;
    }

    void EditorLayer::NewScene()
    {
        const auto& viewportContext = m_ViewportController.GetContext();
        m_SceneSession.CreateNewScene(
            m_ActiveScene,
            static_cast<uint32_t>(viewportContext.Size.x),
            static_cast<uint32_t>(viewportContext.Size.y));

        m_PanelCoordinator.ApplyScene(m_ActiveScene, true);
    }

    void EditorLayer::OpenScene()
    {
        std::string filepath = FileDialogs::OpenFile("*.scene", "场景文件");
        if (!filepath.empty())
            OpenScene(filepath);
    }

    void EditorLayer::OpenScene(const std::string& filepath)
    {
        const auto& viewportContext = m_ViewportController.GetContext();

        EditorRenderSettings renderSettings;
        if (!m_SceneSession.OpenSceneFromPath(
                m_ActiveScene,
                filepath,
                static_cast<uint32_t>(viewportContext.Size.x),
                static_cast<uint32_t>(viewportContext.Size.y),
                &renderSettings))
        {
            return;
        }

        m_PostProcessingSettings = renderSettings.PostProcessing;
        m_ActiveScene->SetPhysicsBackend(static_cast<PhysicsBackend>(renderSettings.PhysicsBackend));

        m_SceneRenderer.GetSSAOEnabled() = renderSettings.SSAOEnabled;
        m_SceneRenderer.GetSSAORadius() = renderSettings.SSAORadius;
        m_SceneRenderer.GetSSAOBias() = renderSettings.SSAOBias;
        m_SceneRenderer.GetSSAOKernelSize() = renderSettings.SSAOKernelSize;
        m_SceneRenderer.GetSSAOIntensity() = renderSettings.SSAOIntensity;

        if (renderSettings.MSAASamples != m_ViewportController.GetHDRFramebuffer()->GetSpecification().Samples)
        {
            m_ViewportController.ApplyMSAASamples(renderSettings.MSAASamples);
            m_SceneRenderer.SetHDRFramebuffer(m_ViewportController.GetHDRFramebuffer());
        }

        m_PanelCoordinator.SetHDRFramebuffer(m_ViewportController.GetHDRFramebuffer());
        m_PanelCoordinator.ApplyScene(m_ActiveScene, true);
    }

    void EditorLayer::SaveScene()
    {
        std::string filepath = FileDialogs::SaveFile("*.scene", "场景文件");
        if (filepath.empty())
            return;

        EditorRenderSettings renderSettings;
        renderSettings.PostProcessing = m_PostProcessingSettings;
        renderSettings.MSAASamples = m_ViewportController.GetHDRFramebuffer()->GetSpecification().Samples;
        renderSettings.PhysicsBackend = static_cast<int>(m_ActiveScene->GetPhysicsBackend());
        renderSettings.SSAOEnabled = m_SceneRenderer.GetSSAOEnabled();
        renderSettings.SSAORadius = m_SceneRenderer.GetSSAORadius();
        renderSettings.SSAOBias = m_SceneRenderer.GetSSAOBias();
        renderSettings.SSAOKernelSize = m_SceneRenderer.GetSSAOKernelSize();
        renderSettings.SSAOIntensity = m_SceneRenderer.GetSSAOIntensity();

        m_SceneSession.SaveSceneToPath(m_ActiveScene, filepath, renderSettings);
    }

    void EditorLayer::OnScenePlay()
    {
        m_SceneSession.BeginPlay(m_ActiveScene);
    }

    void EditorLayer::OnSceneStop()
    {
        m_SceneSession.EndPlay(m_ActiveScene);
        m_PanelCoordinator.ApplyScene(m_ActiveScene, false);
    }

    void EditorLayer::HandleShellActions(const EditorShellActions& actions)
    {
        if (actions.RequestNewScene)
            NewScene();
        if (actions.RequestOpenScene)
            OpenScene();
        if (actions.RequestSaveScene)
            SaveScene();
        if (actions.RequestPlay)
            OnScenePlay();
        if (actions.RequestStop)
            OnSceneStop();
        if (actions.RequestUndo)
            m_CommandHistory.UndoCommand();
        if (actions.RequestRedo)
            m_CommandHistory.RedoCommand();
        if (actions.ToggleStatsPanel)
            m_PanelCoordinator.ToggleStatsPanelVisible();
        if (actions.RequestCloseApplication)
            Application::Get().Close();
    }

    EditorShellState EditorLayer::BuildShellState() const
    {
        EditorShellState state;
        state.CurrentSceneState = m_SceneSession.GetState();
        state.CanUndo = m_CommandHistory.CanUndo();
        state.CanRedo = m_CommandHistory.CanRedo();
        state.UndoDescription = m_CommandHistory.GetUndoDescription();
        state.RedoDescription = m_CommandHistory.GetRedoDescription();
        state.ShowStatsPanel = m_PanelCoordinator.IsStatsPanelVisible();
        return state;
    }

} // namespace Engine
