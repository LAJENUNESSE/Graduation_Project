#include "EditorLayer.h"
#include "Core/Input.h"
#include "Core/Log.h"
#include "Core/FileDialogs.h"
#include "ImGui/ImGuiLayer.h"
#include "Scene/Components.h"
#include "Renderer/Renderer.h"
#include "Renderer/Mesh.h"
#include "Asset/AssetManager.h"
#include "Debug/PerformanceMonitor.h"
#include "Debug/ProfileTimer.h"

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

        // Create framebuffer with multi-attachment:
        //   index 0 = RGBA8 (color)
        //   index 1 = RED_INTEGER (entity ID for picking)
        //   depth = DEPTH24STENCIL8
        FramebufferSpecification fbSpec;
        fbSpec.Attachments = {FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER,
                              FramebufferTextureFormat::DEPTH24STENCIL8};
        fbSpec.Width = 1280;
        fbSpec.Height = 720;
        m_Framebuffer = Framebuffer::Create(fbSpec);

        // HDR framebuffer for scene rendering (RGBA16F for HDR values)
        FramebufferSpecification hdrSpec;
        hdrSpec.Attachments = {FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RED_INTEGER,
                               FramebufferTextureFormat::DEPTH_COMPONENT};
        hdrSpec.Width = 1280;
        hdrSpec.Height = 720;
        m_HDRFramebuffer = Framebuffer::Create(hdrSpec);

        // Initialize post-processing
        m_PostProcessing.Init(1280, 720);

        // Initialize scene renderer
        m_SceneRenderer.Init(static_cast<uint32_t>(m_ViewportSize.x),
                             static_cast<uint32_t>(m_ViewportSize.y));
        m_SceneRenderer.SetHDRFramebuffer(m_HDRFramebuffer);
        m_SceneRenderer.SetPostProcessing(&m_PostProcessing, &m_PostProcessingSettings);
        m_SceneSession.Initialize(&m_SceneRenderer);
        m_SceneRenderer.SetDebugDrawCallback([this]() {
            if (m_ShowPhysicsColliders)
                m_PhysicsDebugDraw.DrawColliders(m_ActiveScene->GetRegistry(), m_EditorCamera);
        });

        // Create scene and a default entity
        m_ActiveScene = CreateRef<Scene>();
        m_ActiveScene->SetSceneRenderer(&m_SceneRenderer);

        Entity cubeEntity = m_ActiveScene->CreateEntity("Cube");
        auto& meshRenderer = cubeEntity.AddComponent<MeshRendererComponent>();
        meshRenderer.Type = MeshType::Cube;
        meshRenderer.MeshAsset = AssetManager::Load<Mesh>("builtin:Cube");
        meshRenderer.Color = {0.8f, 0.2f, 0.3f, 1.0f};

        Entity lightEntity = m_ActiveScene->CreateEntity("\xe6\x96\xb9\xe5\x90\x91\xe5\x85\x89");
        auto& light = lightEntity.AddComponent<LightComponent>();
        light.Type = LightComponent::LightType::Directional;
        light.Color = {1.0f, 0.95f, 0.9f};
        auto& lightTransform = lightEntity.GetComponent<TransformComponent>();
        lightTransform.Rotation = {glm::radians(-45.0f), glm::radians(30.0f), 0.0f};

        // Initialize panels
        m_HierarchyPanel.SetCommandHistory(&m_CommandHistory);
        m_AssetBrowserPanel.SetSceneOpenCallback([this](const std::string& path) {
            OpenScene(path);
        });

        // 初始化渲染设置面板
        m_RenderSettingsPanel.SetContext(&m_SceneRenderer, &m_PostProcessingSettings,
                                          m_HDRFramebuffer, m_ActiveScene, &m_ShowPhysicsColliders);
        m_RenderSettingsPanel.SetMSAAChangedCallback([this](uint32_t samples) {
            FramebufferSpecification spec = m_HDRFramebuffer->GetSpecification();
            spec.Samples = samples;
            m_HDRFramebuffer = Framebuffer::Create(spec);
            m_SceneRenderer.SetHDRFramebuffer(m_HDRFramebuffer);
            m_PanelCoordinator.SetHDRFramebuffer(m_HDRFramebuffer);
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
        // 注册控制台 sink 到 spdlog
        m_ConsolePanel.RegisterSink();

        // Initialize editor camera
        m_EditorCamera = EditorCamera(45.0f, 1280.0f / 720.0f, 0.1f, 1000.0f);
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
        // Handle FBO resize before rendering
        FramebufferSpecification spec = m_Framebuffer->GetSpecification();
        if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f &&
            (spec.Width != static_cast<uint32_t>(m_ViewportSize.x) ||
             spec.Height != static_cast<uint32_t>(m_ViewportSize.y)))
        {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                uint32_t w = static_cast<uint32_t>(m_ViewportSize.x);
                uint32_t h = static_cast<uint32_t>(m_ViewportSize.y);

                m_Framebuffer->Resize(w, h);
                m_SceneRenderer.ResizeHDR(w, h);
                m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
                m_ActiveScene->OnViewportResize(w, h);
            }
        }

        m_EditorCamera.OnUpdate(ts, m_ViewportHovered);

        // 更新 AssetManager（异步加载轮询 + 热重载检测）
        AssetManager::Update(ts);

        // 物理更新（仅运行时）
        switch (m_SceneSession.GetState())
        {
        case SceneState::Edit:
            m_ActiveScene->OnUpdateEditor(ts, m_EditorCamera);
            break;
        case SceneState::Play:
            m_ActiveScene->OnUpdateRuntime(ts, m_EditorCamera);
            break;
        }

        // 渲染：完整管线由 SceneRenderer 统一编排
        float sceneRenderCpuMs = 0.0f;
        {
            PROFILE_SCOPE("SceneRender", &sceneRenderCpuMs);
            m_SceneRenderer.BeginScene(m_EditorCamera, m_ActiveScene.get(), ts);
            m_SceneRenderer.RenderPipeline(m_Framebuffer);
            m_SceneRenderer.EndScene();
        }
        PerformanceMonitor::Get().SetSceneRenderCPU(sceneRenderCpuMs);
    }

        void EditorLayer::OnImGuiRender()
    {
        HandleShellActions(m_EditorShell.Draw(BuildShellState()));

        m_PanelCoordinator.RenderPanels();

        // Viewport
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("\xe8\xa7\x86\xe5\x8f\xa3");

        auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
        auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
        auto viewportOffset = ImGui::GetWindowPos();
        m_ViewportBounds[0] = {viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y};
        m_ViewportBounds[1] = {viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y};

        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();
        Application::Get().GetImGuiLayer()->SetBlockEvents(!m_ViewportHovered);

        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        m_ViewportSize = {std::max(viewportPanelSize.x, 32.0f), std::max(viewportPanelSize.y, 32.0f)};

        // Mouse picking：仅在鼠标点击时读取像素（glReadPixels 会导致 GPU stall，
        // 每帧在鼠标移动时调用会严重降低帧率）
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                ImVec2 mousePos = ImGui::GetMousePos();
                float mx = mousePos.x - m_ViewportBounds[0].x;
                float my = mousePos.y - m_ViewportBounds[0].y;
                glm::vec2 vpSize = m_ViewportBounds[1] - m_ViewportBounds[0];
                my = vpSize.y - my;

                int mouseX = static_cast<int>(mx);
                int mouseY = static_cast<int>(my);

                if (mouseX >= 0 && mouseY >= 0 && mouseX < static_cast<int>(vpSize.x) &&
                    mouseY < static_cast<int>(vpSize.y))
                {
                    int pixelData = m_HDRFramebuffer->ReadPixel(1, mouseX, mouseY);
                    m_HoveredEntity =
                        pixelData == -1 ? Entity() : Entity(static_cast<entt::entity>(pixelData), m_ActiveScene.get());
                }
            }
        }

        uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID(0);
        ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(textureID)),
                     ImVec2(m_ViewportSize.x, m_ViewportSize.y), ImVec2(0, 1), ImVec2(1, 0));

        // ImGuizmo setup
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y,
                          m_ViewportBounds[1].x - m_ViewportBounds[0].x,
                          m_ViewportBounds[1].y - m_ViewportBounds[0].y);

        // Gizmos（支持多选：主选中实体显示 Gizmo，移动时带动所有选中实体）
        auto& selectedEntities = m_HierarchyPanel.GetSelectedEntities();
        if (m_SelectedEntity && m_GizmoType != -1 && m_SelectedEntity.HasComponent<TransformComponent>())
        {
            const glm::mat4& cameraView = m_EditorCamera.GetViewMatrix();
            const glm::mat4& cameraProjection = m_EditorCamera.GetProjection();

            auto& tc = m_SelectedEntity.GetComponent<TransformComponent>();

            // 使用世界变换矩阵显示 Gizmo
            glm::mat4 worldTransform = m_ActiveScene->GetWorldTransform(m_SelectedEntity);

            // 计算父实体的世界变换（用于将 Gizmo 结果转回本地空间）
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

            // 记录操作前的世界位置（用于计算移动增量）
            glm::vec3 prevWorldPos = glm::vec3(worldTransform[3]);

            glm::mat4 transform = worldTransform;

            ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
                                 static_cast<ImGuizmo::OPERATION>(m_GizmoType), ImGuizmo::LOCAL,
                                 glm::value_ptr(transform));

            if (ImGuizmo::IsUsing())
            {
                // 捕获 Gizmo 拖拽开始时的 Transform 快照
                if (!m_GizmoWasUsing)
                {
                    m_GizmoStartTranslation = tc.Translation;
                    m_GizmoStartRotation = tc.Rotation;
                    m_GizmoStartScale = tc.Scale;
                    m_GizmoWasUsing = true;
                }

                // 将世界空间的 Gizmo 结果转回本地空间
                glm::mat4 localTransform = glm::inverse(parentWorldTransform) * transform;

                glm::vec3 translation, rotation, scale;
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(localTransform), glm::value_ptr(translation),
                                                      glm::value_ptr(rotation), glm::value_ptr(scale));

                glm::vec3 deltaRotation = glm::radians(rotation) - tc.Rotation;
                tc.Translation = translation;
                tc.Rotation += deltaRotation;
                tc.Scale = scale;

                // 多选模式：平移操作时，将世界空间的移动增量应用到其他选中实体
                if (selectedEntities.size() > 1 && m_GizmoType == ImGuizmo::TRANSLATE)
                {
                    glm::vec3 newWorldPos = glm::vec3(transform[3]);
                    glm::vec3 worldDelta = newWorldPos - prevWorldPos;

                    for (const auto& otherEntity : selectedEntities)
                    {
                        if (otherEntity == m_SelectedEntity || !otherEntity)
                            continue;

                        // 使用 registry 直接操作，避免 const Entity 的限制
                        auto handle = static_cast<entt::entity>(otherEntity);
                        auto& reg = m_ActiveScene->GetRegistry();

                        if (!reg.all_of<TransformComponent>(handle))
                            continue;

                        auto& otherTc = reg.get<TransformComponent>(handle);

                        // 将世界增量转换到其他实体的本地空间
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

                        // 将世界空间增量旋转到本地空间
                        glm::vec3 localDelta = glm::vec3(glm::inverse(otherParentWorld) * glm::vec4(worldDelta, 0.0f));
                        otherTc.Translation += localDelta;
                    }
                }
            }
            else if (m_GizmoWasUsing)
            {
                // Gizmo 拖拽结束，创建 Undo 命令
                m_GizmoWasUsing = false;
                auto cmd = CreateRef<TransformChangeCommand>(
                    m_SelectedEntity,
                    m_GizmoStartTranslation, m_GizmoStartRotation, m_GizmoStartScale,
                    tc.Translation, tc.Rotation, tc.Scale);
                m_CommandHistory.PushCommand(cmd);
            }
        }

        // ViewManipulate
        {
            glm::mat4 viewMatrix = m_EditorCamera.GetViewMatrix();
            float viewManipulateRight = m_ViewportBounds[1].x;
            float viewManipulateTop = m_ViewportBounds[0].y;
            ImGuizmo::ViewManipulate(glm::value_ptr(viewMatrix), m_EditorCamera.GetDistance(),
                                     ImVec2(viewManipulateRight - 128, viewManipulateTop), ImVec2(128, 128),
                                     0x10101010);
            if (viewMatrix != m_EditorCamera.GetViewMatrix())
                m_EditorCamera.SetViewMatrix(viewMatrix);
        }

        ImGui::End();
        ImGui::PopStyleVar();

    }

    void EditorLayer::OnEvent(Event& event)
    {
        m_EditorCamera.OnEvent(event);

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
            if (m_ViewportHovered && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing() &&
                !Input::IsKeyPressed(KeyCode::LeftAlt))
            {
                m_HierarchyPanel.SetSelectedEntity(m_HoveredEntity);
            }
        }
        return false;
    }

    void EditorLayer::NewScene()
    {
        m_SceneSession.CreateNewScene(
            m_ActiveScene,
            static_cast<uint32_t>(m_ViewportSize.x),
            static_cast<uint32_t>(m_ViewportSize.y));

        m_PanelCoordinator.ApplyScene(m_ActiveScene, true);
    }

    void EditorLayer::OpenScene()
    {
        std::string filepath = FileDialogs::OpenFile("*.scene", "\xe5\x9c\xba\xe6\x99\xaf\xe6\x96\x87\xe4\xbb\xb6");
        if (!filepath.empty())
            OpenScene(filepath);
    }

    void EditorLayer::OpenScene(const std::string& filepath)
    {
        EditorRenderSettings renderSettings;
        if (!m_SceneSession.OpenSceneFromPath(
                m_ActiveScene,
                filepath,
                static_cast<uint32_t>(m_ViewportSize.x),
                static_cast<uint32_t>(m_ViewportSize.y),
                &renderSettings))
        {
            return;
        }

        m_PostProcessingSettings = renderSettings.PostProcessing;
        m_ActiveScene->SetPhysicsBackend(static_cast<PhysicsBackend>(renderSettings.PhysicsBackend));

        // 恢复 SSAO 设置
        m_SceneRenderer.GetSSAOEnabled() = renderSettings.SSAOEnabled;
        m_SceneRenderer.GetSSAORadius() = renderSettings.SSAORadius;
        m_SceneRenderer.GetSSAOBias() = renderSettings.SSAOBias;
        m_SceneRenderer.GetSSAOKernelSize() = renderSettings.SSAOKernelSize;
        m_SceneRenderer.GetSSAOIntensity() = renderSettings.SSAOIntensity;

        if (renderSettings.MSAASamples != m_HDRFramebuffer->GetSpecification().Samples)
        {
            FramebufferSpecification spec = m_HDRFramebuffer->GetSpecification();
            spec.Samples = renderSettings.MSAASamples;
            m_HDRFramebuffer = Framebuffer::Create(spec);
            m_SceneRenderer.SetHDRFramebuffer(m_HDRFramebuffer);
        }

        m_PanelCoordinator.SetHDRFramebuffer(m_HDRFramebuffer);
        m_PanelCoordinator.ApplyScene(m_ActiveScene, true);
    }

    void EditorLayer::SaveScene()
    {
        std::string filepath = FileDialogs::SaveFile("*.scene", "场景文件");
        if (filepath.empty())
            return;

        EditorRenderSettings renderSettings;
        renderSettings.PostProcessing = m_PostProcessingSettings;
        renderSettings.MSAASamples = m_HDRFramebuffer->GetSpecification().Samples;
        renderSettings.PhysicsBackend = static_cast<int>(m_ActiveScene->GetPhysicsBackend());

        // 保存 SSAO 设置
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
