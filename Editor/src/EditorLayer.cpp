#include "EditorLayer.h"
#include "Core/Input.h"
#include "Core/Log.h"
#include "Core/FileDialogs.h"
#include "ImGui/ImGuiLayer.h"
#include "Scene/Components.h"
#include "Scene/SceneSerializer.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Mesh.h"
#include "Debug/PerformanceMonitor.h"
#include "Debug/ProfileTimer.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <filesystem>

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

        // Create scene and a default entity
        m_ActiveScene = CreateRef<Scene>();

        // Create a default cube entity
        Entity cubeEntity = m_ActiveScene->CreateEntity("Cube");
        auto& meshRenderer = cubeEntity.AddComponent<MeshRendererComponent>();
        meshRenderer.MeshData = Mesh::CreateCube();
        meshRenderer.Color = {0.8f, 0.2f, 0.3f, 1.0f};

        // Create a default directional light
        Entity lightEntity = m_ActiveScene->CreateEntity("方向光");
        auto& light = lightEntity.AddComponent<LightComponent>();
        light.Type = LightComponent::LightType::Directional;
        light.Color = {1.0f, 0.95f, 0.9f};
        auto& lightTransform = lightEntity.GetComponent<TransformComponent>();
        lightTransform.Rotation = {glm::radians(-45.0f), glm::radians(30.0f), 0.0f};

        // Initialize panels
        m_HierarchyPanel.SetContext(m_ActiveScene);

        // Initialize editor camera
        m_EditorCamera = EditorCamera(45.0f, 1280.0f / 720.0f, 0.1f, 1000.0f);
    }

    void EditorLayer::OnDetach()
    {
    }

    void EditorLayer::OnUpdate(Timestep ts)
    {
        // Handle FBO resize before rendering (only when mouse is released to avoid rebuild spam)
        FramebufferSpecification spec = m_Framebuffer->GetSpecification();
        if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f &&
            (spec.Width != static_cast<uint32_t>(m_ViewportSize.x) ||
             spec.Height != static_cast<uint32_t>(m_ViewportSize.y)))
        {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                m_Framebuffer->Resize(static_cast<uint32_t>(m_ViewportSize.x),
                                      static_cast<uint32_t>(m_ViewportSize.y));
                m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
                m_ActiveScene->OnViewportResize(static_cast<uint32_t>(m_ViewportSize.x),
                                                static_cast<uint32_t>(m_ViewportSize.y));
            }
        }

        m_EditorCamera.OnUpdate(ts, m_ViewportHovered);

        // Shadow pass (renders to its own FBO) — CPU profiled
        float shadowCpuMs = 0.0f;
        {
            PROFILE_SCOPE("ShadowPass", &shadowCpuMs);
            m_ActiveScene->ShadowPass();
        }
        PerformanceMonitor::Get().SetShadowPassCPU(shadowCpuMs);

        // Render — CPU profiled
        float sceneRenderCpuMs = 0.0f;
        {
            PROFILE_SCOPE("SceneRender", &sceneRenderCpuMs);
            m_Framebuffer->Bind();
            RenderCommand::SetClearColor({0.1f, 0.1f, 0.1f, 1.0f});
            RenderCommand::Clear();

            // Clear entity ID attachment to -1 (no entity)
            m_Framebuffer->ClearAttachment(1, -1);

            // Scene rendering
            m_ActiveScene->OnUpdateEditor(ts, m_EditorCamera);

            m_Framebuffer->Unbind();
        }
        PerformanceMonitor::Get().SetSceneRenderCPU(sceneRenderCpuMs);
    }

    void EditorLayer::OnImGuiRender()
    {
        // Full-screen dockspace
        static bool dockspaceOpen = true;
        static ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                       ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("DockSpace", &dockspaceOpen, windowFlags);
        ImGui::PopStyleVar(3);

        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGuiID dockspaceId = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
        }

        // Menu bar
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("文件"))
            {
                if (ImGui::MenuItem("新建场景", "Ctrl+N"))
                    NewScene();
                if (ImGui::MenuItem("打开场景...", "Ctrl+O"))
                    OpenScene();
                if (ImGui::MenuItem("保存场景...", "Ctrl+Shift+S"))
                    SaveScene();
                ImGui::Separator();
                if (ImGui::MenuItem("退出"))
                    Application::Get().Close();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("\xe8\xa7\x86\xe5\x9b\xbe"))
            {
                ImGui::MenuItem("\xe6\x80\xa7\xe8\x83\xbd\xe7\x9b\x91\xe6\x8e\xa7", nullptr, &m_ShowStatsPanel);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // Panels
        m_HierarchyPanel.OnImGuiRender();
        m_SelectedEntity = m_HierarchyPanel.GetSelectedEntity();
        m_PropertiesPanel.OnImGuiRender(m_SelectedEntity);

        // Rendering settings panel
        {
            auto& shadow = m_ActiveScene->GetShadowSettings();
            ImGui::Begin("渲染设置");

            ImGui::Checkbox("阴影", &shadow.Enabled);

            const char* resolutionItems[] = {"512", "1024", "2048"};
            int resolutionValues[] = {512, 1024, 2048};
            int currentIdx = 1; // default 1024
            for (int i = 0; i < 3; i++)
            {
                if (shadow.MapResolution == resolutionValues[i])
                {
                    currentIdx = i;
                    break;
                }
            }
            if (ImGui::Combo("阴影分辨率", &currentIdx, resolutionItems, 3))
            {
                m_ActiveScene->ResizeShadowMap(resolutionValues[currentIdx]);
            }

            ImGui::DragFloat("阴影偏移", &shadow.Bias, 0.001f, 0.0f, 0.05f, "%.4f");
            ImGui::DragFloat("阴影范围", &shadow.OrthoSize, 0.5f, 5.0f, 100.0f, "%.1f");

            ImGui::Separator();
            ImGui::Text("\xe5\xa4\xa9\xe7\xa9\xba\xe7\x9b\x92");

            if (m_ActiveScene->HasSkybox())
            {
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "\xe5\xb7\xb2\xe5\x8a\xa0\xe8\xbd\xbd");
                if (ImGui::Button("\xe6\xb8\x85\xe9\x99\xa4\xe5\xa4\xa9\xe7\xa9\xba\xe7\x9b\x92"))
                    m_ActiveScene->ClearSkybox();
            }
            else
            {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "\xe6\x9c\xaa\xe5\x8a\xa0\xe8\xbd\xbd");
            }

            if (ImGui::Button("\xe5\x8a\xa0\xe8\xbd\xbd\xe5\xa4\xa9\xe7\xa9\xba\xe7\x9b\x92..."))
            {
                std::string dir = FileDialogs::OpenFile("*.jpg *.png *.tga", "\xe5\xa4\xa9\xe7\xa9\xba\xe7\x9b\x92\xe6\x96\x87\xe4\xbb\xb6\xe5\xa4\xb9\xe4\xb8\xad\xe4\xbb\xbb\xe6\x84\x8f\xe4\xb8\x80\xe5\xbc\xa0\xe5\x9b\xbe");
                if (!dir.empty())
                {
                    // Derive folder and try standard naming: right/left/top/bottom/front/back
                    std::filesystem::path p(dir);
                    std::filesystem::path folder = p.parent_path();
                    std::string ext = p.extension().string();

                    std::vector<std::string> suffixes = {"right", "left", "top", "bottom", "front", "back"};
                    std::vector<std::string> faces;
                    bool allFound = true;
                    for (const auto& s : suffixes)
                    {
                        std::filesystem::path facePath = folder / (s + ext);
                        if (std::filesystem::exists(facePath))
                            faces.push_back(facePath.string());
                        else
                        {
                            allFound = false;
                            break;
                        }
                    }

                    if (allFound)
                        m_ActiveScene->LoadSkybox(faces);
                    else
                        ENGINE_WARN("Skybox needs: right/left/top/bottom/front/back{}", ext);
                }
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("right/left/top/bottom/front/back + .jpg/.png");

            ImGui::End();
        }

        // Performance monitoring panel
        if (m_ShowStatsPanel)
        {
            auto& pm = PerformanceMonitor::Get();

            ImGui::Begin("\xe6\x80\xa7\xe8\x83\xbd\xe7\x9b\x91\xe6\x8e\xa7", &m_ShowStatsPanel);

            ImGui::Text("FPS: %.1f", pm.GetFPS());
            ImGui::Text("\xe5\xb8\xa7\xe6\x97\xb6\xe9\x97\xb4: %.2f ms", pm.GetFrameTimeMs());

            ImGui::Separator();
            ImGui::Text("CPU \xe8\x80\x97\xe6\x97\xb6:");
            ImGui::Text("  \xe9\x98\xb4\xe5\xbd\xb1Pass:  %.3f ms", pm.GetShadowPassCpuMs());
            ImGui::Text("  \xe5\x9c\xba\xe6\x99\xaf\xe6\xb8\xb2\xe6\x9f\x93:  %.3f ms", pm.GetSceneRenderCpuMs());
            ImGui::Text("  ImGui:     %.3f ms", pm.GetImGuiCpuMs());

            ImGui::Separator();
            ImGui::Text("GPU \xe8\x80\x97\xe6\x97\xb6 (\xe4\xb8\x8a\xe4\xb8\x80\xe5\xb8\xa7):");
            ImGui::Text("  \xe9\x98\xb4\xe5\xbd\xb1Pass:  %.3f ms", pm.GetShadowPassGpuMs());
            ImGui::Text("  \xe5\x9c\xba\xe6\x99\xaf\xe6\xb8\xb2\xe6\x9f\x93:  %.3f ms", pm.GetSceneRenderGpuMs());

            ImGui::Separator();
            const auto& stats = pm.GetStats();
            ImGui::Text("\xe6\xb8\xb2\xe6\x9f\x93\xe7\xbb\x9f\xe8\xae\xa1:");
            ImGui::Text("  Draw Calls: %u", stats.DrawCalls);
            ImGui::Text("  \xe9\xa1\xb6\xe7\x82\xb9\xe6\x95\xb0: %u", stats.Vertices);
            ImGui::Text("  \xe4\xb8\x89\xe8\xa7\x92\xe5\xbd\xa2: %u", stats.Triangles);

            ImGui::Separator();
            ImGui::Text("\xe5\xb8\xa7\xe6\x97\xb6\xe9\x97\xb4\xe5\x8e\x86\xe5\x8f\xb2:");
            ImGui::PlotLines("##FrameTime", pm.GetFrameTimeHistory(),
                             PerformanceMonitor::FrameHistorySize,
                             pm.GetFrameTimeHistoryOffset(),
                             nullptr, 0.0f, 33.3f, ImVec2(0, 80));

            ImGui::End();
        }

        // Viewport
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("视口");

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

        // Mouse picking — only sample when mouse moved or clicked (avoid per-frame glReadPixels stall)
        {
            ImVec2 mousePos = ImGui::GetMousePos();
            glm::vec2 currentMousePos = {mousePos.x, mousePos.y};
            bool mouseMoved = (currentMousePos != m_LastMousePos);
            m_LastMousePos = currentMousePos;

            if (mouseMoved || ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                float mx = mousePos.x - m_ViewportBounds[0].x;
                float my = mousePos.y - m_ViewportBounds[0].y;
                glm::vec2 vpSize = m_ViewportBounds[1] - m_ViewportBounds[0];
                my = vpSize.y - my; // Flip Y

                int mouseX = static_cast<int>(mx);
                int mouseY = static_cast<int>(my);

                if (mouseX >= 0 && mouseY >= 0 && mouseX < static_cast<int>(vpSize.x) &&
                    mouseY < static_cast<int>(vpSize.y))
                {
                    int pixelData = m_Framebuffer->ReadPixel(1, mouseX, mouseY);
                    m_HoveredEntity =
                        pixelData == -1 ? Entity() : Entity(static_cast<entt::entity>(pixelData), m_ActiveScene.get());
                }
            }
        }

        uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID(0);
        ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(textureID)),
                     ImVec2(m_ViewportSize.x, m_ViewportSize.y), ImVec2(0, 1), ImVec2(1, 0));

        // ImGuizmo setup (shared by Gizmos and ViewManipulate)
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y,
                          m_ViewportBounds[1].x - m_ViewportBounds[0].x,
                          m_ViewportBounds[1].y - m_ViewportBounds[0].y);

        // Gizmos
        if (m_SelectedEntity && m_GizmoType != -1 && m_SelectedEntity.HasComponent<TransformComponent>())
        {
            // Editor camera
            const glm::mat4& cameraView = m_EditorCamera.GetViewMatrix();
            const glm::mat4& cameraProjection = m_EditorCamera.GetProjection();

            // Entity transform
            auto& tc = m_SelectedEntity.GetComponent<TransformComponent>();
            glm::mat4 transform = tc.GetTransform();

            ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
                                 static_cast<ImGuizmo::OPERATION>(m_GizmoType), ImGuizmo::LOCAL,
                                 glm::value_ptr(transform));

            if (ImGuizmo::IsUsing())
            {
                // Decompose the resulting matrix back to translation/rotation/scale
                glm::vec3 translation, rotation, scale;
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(transform), glm::value_ptr(translation),
                                                      glm::value_ptr(rotation), glm::value_ptr(scale));

                glm::vec3 deltaRotation = glm::radians(rotation) - tc.Rotation;
                tc.Translation = translation;
                tc.Rotation += deltaRotation;
                tc.Scale = scale;
            }
        }

        // 视口方向指示器（右上角 128x128）
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

        ImGui::End(); // DockSpace
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
        // Shortcuts
        if (e.GetRepeatCount() > 0)
            return false;

        bool control = Input::IsKeyPressed(KeyCode::LeftControl) || Input::IsKeyPressed(KeyCode::RightControl);
        bool shift = Input::IsKeyPressed(KeyCode::LeftShift) || Input::IsKeyPressed(KeyCode::RightShift);

        switch (static_cast<KeyCode>(e.GetKeyCode()))
        {
        case KeyCode::N:
            if (control)
                NewScene();
            break;
        case KeyCode::O:
            if (control)
                OpenScene();
            break;
        case KeyCode::S:
            if (control && shift)
                SaveScene();
            break;

        // Gizmos
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
        // Mouse picking
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
        m_ActiveScene = CreateRef<Scene>();
        m_ActiveScene->OnViewportResize(static_cast<uint32_t>(m_ViewportSize.x),
                                        static_cast<uint32_t>(m_ViewportSize.y));
        m_HierarchyPanel.SetContext(m_ActiveScene);
        m_SelectedEntity = {};
        m_HoveredEntity = {};
    }

    void EditorLayer::OpenScene()
    {
        std::string filepath = FileDialogs::OpenFile("*.scene", "场景文件");
        if (filepath.empty())
            return;

        // Transactional load: deserialize into a temporary scene first
        auto newScene = CreateRef<Scene>();
        SceneSerializer serializer(newScene);
        if (!serializer.Deserialize(filepath))
        {
            ENGINE_WARN("Failed to load scene from '{0}', keeping current scene", filepath);
            return;
        }

        // Success — swap in the new scene
        m_ActiveScene = newScene;
        m_ActiveScene->OnViewportResize(static_cast<uint32_t>(m_ViewportSize.x),
                                        static_cast<uint32_t>(m_ViewportSize.y));
        m_HierarchyPanel.SetContext(m_ActiveScene);
        m_SelectedEntity = {};
        m_HoveredEntity = {};
    }

    void EditorLayer::SaveScene()
    {
        std::string filepath = FileDialogs::SaveFile("*.scene", "场景文件");
        if (filepath.empty())
            return;

        SceneSerializer serializer(m_ActiveScene);
        if (serializer.Serialize(filepath))
            ENGINE_INFO("Scene saved to '{0}'", filepath);
        else
            ENGINE_WARN("Failed to save scene to '{0}'", filepath);
    }

} // namespace Engine
