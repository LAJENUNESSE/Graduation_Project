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
#include "Asset/AssetManager.h"
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

        // HDR framebuffer for scene rendering (RGBA16F for HDR values)
        FramebufferSpecification hdrSpec;
        hdrSpec.Attachments = {FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RED_INTEGER,
                               FramebufferTextureFormat::DEPTH24STENCIL8};
        hdrSpec.Width = 1280;
        hdrSpec.Height = 720;
        m_HDRFramebuffer = Framebuffer::Create(hdrSpec);

        // Initialize post-processing
        m_PostProcessing.Init(1280, 720);

        // Initialize scene renderer
        m_SceneRenderer.Init();

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
        m_HierarchyPanel.SetContext(m_ActiveScene);

        // Initialize editor camera
        m_EditorCamera = EditorCamera(45.0f, 1280.0f / 720.0f, 0.1f, 1000.0f);
    }

    void EditorLayer::OnDetach()
    {
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
                m_Framebuffer->Resize(static_cast<uint32_t>(m_ViewportSize.x),
                                      static_cast<uint32_t>(m_ViewportSize.y));
                m_HDRFramebuffer->Resize(static_cast<uint32_t>(m_ViewportSize.x),
                                         static_cast<uint32_t>(m_ViewportSize.y));
                m_PostProcessing.Resize(static_cast<uint32_t>(m_ViewportSize.x),
                                        static_cast<uint32_t>(m_ViewportSize.y));
                m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
                m_ActiveScene->OnViewportResize(static_cast<uint32_t>(m_ViewportSize.x),
                                                static_cast<uint32_t>(m_ViewportSize.y));
            }
        }

        m_EditorCamera.OnUpdate(ts, m_ViewportHovered);

        // 更新 AssetManager（异步加载轮询 + 热重载检测）
        AssetManager::Update(ts);

        // 物理更新（仅运行时）
        switch (m_SceneState)
        {
        case SceneState::Edit:
            m_ActiveScene->OnUpdateEditor(ts, m_EditorCamera);
            break;
        case SceneState::Play:
            m_ActiveScene->OnUpdateRuntime(ts, m_EditorCamera);
            break;
        }

        // 开始场景渲染
        m_SceneRenderer.BeginScene(m_EditorCamera, m_ActiveScene.get(), ts);

        // Shadow pass (renders to its own FBO) — CPU profiled
        float shadowCpuMs = 0.0f;
        {
            PROFILE_SCOPE("ShadowPass", &shadowCpuMs);
            // 手动执行 LightCollect + ShadowPass
            for (auto& pass : m_SceneRenderer.GetPassQueue())
            {
                if (pass.Enabled && (pass.Name == "LightCollect" || pass.Name == "ShadowPass"))
                    pass.ExecuteFn(m_SceneRenderer.GetContext());
            }
        }
        PerformanceMonitor::Get().SetShadowPassCPU(shadowCpuMs);

        // Render — CPU profiled
        float sceneRenderCpuMs = 0.0f;
        {
            PROFILE_SCOPE("SceneRender", &sceneRenderCpuMs);

            // Render scene to HDR FBO
            m_HDRFramebuffer->Bind();
            RenderCommand::SetClearColor({0.1f, 0.1f, 0.1f, 1.0f});
            RenderCommand::Clear();
            m_HDRFramebuffer->ClearAttachment(1, -1);

            const bool msaaEnabled = m_HDRFramebuffer->IsMSAAEnabled();

            // 执行 GeometryPass + SkyboxPass (+ ParticlePass when MSAA is off)
            // MSAA 开启时，粒子会在 Resolve 后再绘制，避免被 Blit 覆盖。
            for (auto& pass : m_SceneRenderer.GetPassQueue())
            {
                bool runPass = pass.Enabled && (pass.Name == "GeometryPass" || pass.Name == "SkyboxPass");
                if (!msaaEnabled && pass.Enabled && pass.Name == "ParticlePass")
                    runPass = true;

                if (runPass)
                    pass.ExecuteFn(m_SceneRenderer.GetContext());
            }

            m_HDRFramebuffer->Unbind();

            // MSAA: re-render geometry+skybox to MSAA FBO, then blit
            if (msaaEnabled)
            {
                m_HDRFramebuffer->BindMSAA();
                RenderCommand::SetClearColor({0.1f, 0.1f, 0.1f, 1.0f});
                RenderCommand::Clear();

                m_SceneRenderer.RenderGeometryAndSkybox();

                m_HDRFramebuffer->BlitMSAA();

                // Resolve 后单独绘制粒子，避免被 MSAA blit 覆盖掉。
                m_HDRFramebuffer->Bind();
                m_SceneRenderer.RenderParticlePass();
                m_HDRFramebuffer->Unbind();
            }

            // 物理碰撞体调试绘制
            if (m_ShowPhysicsColliders)
            {
                m_HDRFramebuffer->Bind();
                m_PhysicsDebugDraw.DrawColliders(m_ActiveScene->GetRegistry(), m_EditorCamera);
                m_HDRFramebuffer->Unbind();
            }

            // Post-processing: HDR -> LDR
            m_Framebuffer->Bind();
            RenderCommand::SetClearColor({0.0f, 0.0f, 0.0f, 1.0f});
            RenderCommand::Clear();
            m_Framebuffer->ClearAttachment(1, -1);

            m_PostProcessing.Process(m_HDRFramebuffer->GetColorAttachmentRendererID(0),
                                     m_PostProcessingSettings);

            m_Framebuffer->Unbind();

            m_SceneRenderer.EndScene();
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
            if (ImGui::BeginMenu("\xe6\x96\x87\xe4\xbb\xb6"))
            {
                if (ImGui::MenuItem("\xe6\x96\xb0\xe5\xbb\xba\xe5\x9c\xba\xe6\x99\xaf", "Ctrl+N"))
                    NewScene();
                if (ImGui::MenuItem("\xe6\x89\x93\xe5\xbc\x80\xe5\x9c\xba\xe6\x99\xaf...", "Ctrl+O"))
                    OpenScene();
                if (ImGui::MenuItem("\xe4\xbf\x9d\xe5\xad\x98\xe5\x9c\xba\xe6\x99\xaf...", "Ctrl+Shift+S"))
                    SaveScene();
                ImGui::Separator();
                if (ImGui::MenuItem("\xe9\x80\x80\xe5\x87\xba"))
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

        // Play/Stop toolbar
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 2));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));

            ImGui::Begin("##\xe5\xb7\xa5\xe5\x85\xb7\xe6\xa0\x8f", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse);

            float windowWidth = ImGui::GetWindowContentRegionMax().x;
            float buttonWidth = 80.0f;
            float buttonHeight = 28.0f;

            ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
            ImGui::SetCursorPosY((ImGui::GetWindowHeight() - buttonHeight) * 0.5f);

            if (m_SceneState == SceneState::Edit)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.5f, 0.15f, 1.0f));
                if (ImGui::Button("\xe2\x96\xb6 \xe6\x92\xad\xe6\x94\xbe", ImVec2(buttonWidth, buttonHeight)))
                    OnScenePlay();
                ImGui::PopStyleColor(3);
            }
            else if (m_SceneState == SceneState::Play)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
                if (ImGui::Button("\xe2\x96\xa0 \xe5\x81\x9c\xe6\xad\xa2", ImVec2(buttonWidth, buttonHeight)))
                    OnSceneStop();
                ImGui::PopStyleColor(3);
            }

            ImGui::PopStyleVar(2);
            ImGui::End();
        }

        // Panels
        m_HierarchyPanel.OnImGuiRender();
        m_SelectedEntity = m_HierarchyPanel.GetSelectedEntity();
        m_PropertiesPanel.OnImGuiRender(m_SelectedEntity);

        // Rendering settings panel
        {
            auto& shadow = m_SceneRenderer.GetShadowSystem().GetSettings();
            ImGui::Begin("\xe6\xb8\xb2\xe6\x9f\x93\xe8\xae\xbe\xe7\xbd\xae");

            ImGui::Checkbox("\xe9\x98\xb4\xe5\xbd\xb1", &shadow.Enabled);

            const char* resolutionItems[] = {"512", "1024", "2048"};
            int resolutionValues[] = {512, 1024, 2048};
            int currentIdx = 1;
            for (int i = 0; i < 3; i++)
            {
                if (shadow.MapResolution == resolutionValues[i])
                {
                    currentIdx = i;
                    break;
                }
            }
            if (ImGui::Combo("\xe9\x98\xb4\xe5\xbd\xb1\xe5\x88\x86\xe8\xbe\xa8\xe7\x8e\x87", &currentIdx, resolutionItems, 3))
            {
                m_SceneRenderer.GetShadowSystem().ResizeShadowMap(resolutionValues[currentIdx]);
            }

            ImGui::DragFloat("\xe9\x98\xb4\xe5\xbd\xb1\xe5\x81\x8f\xe7\xa7\xbb", &shadow.Bias, 0.001f, 0.0f, 0.05f, "%.4f");
            ImGui::DragFloat("\xe9\x98\xb4\xe5\xbd\xb1\xe8\x8c\x83\xe5\x9b\xb4", &shadow.OrthoSize, 0.5f, 5.0f, 100.0f, "%.1f");

            ImGui::Separator();
            ImGui::Checkbox("Gamma \xe6\xa0\xa1\xe6\xad\xa3", &m_PostProcessingSettings.GammaCorrection);

            ImGui::Separator();
            ImGui::Text("\xe5\x90\x8e\xe5\xa4\x84\xe7\x90\x86");
            ImGui::Checkbox("\xe6\xb3\x9b\xe5\x85\x89 (Bloom)", &m_PostProcessingSettings.BloomEnabled);
            if (m_PostProcessingSettings.BloomEnabled)
            {
                ImGui::DragFloat("\xe6\xb3\x9b\xe5\x85\x89\xe9\x98\x88\xe5\x80\xbc", &m_PostProcessingSettings.BloomThreshold, 0.05f, 0.0f, 10.0f, "%.2f");
                ImGui::DragFloat("\xe6\xb3\x9b\xe5\x85\x89\xe5\xbc\xba\xe5\xba\xa6", &m_PostProcessingSettings.BloomStrength, 0.01f, 0.0f, 3.0f, "%.2f");
                ImGui::DragInt("\xe6\xb3\x9b\xe5\x85\x89\xe8\xbf\xad\xe4\xbb\xa3", &m_PostProcessingSettings.BloomIterations, 1, 1, 10);
            }

            const char* toneMappingItems[] = {"Reinhard", "ACES"};
            ImGui::Combo("\xe8\x89\xb2\xe8\xb0\x83\xe6\x98\xa0\xe5\xb0\x84", &m_PostProcessingSettings.ToneMappingMode, toneMappingItems, 2);

            ImGui::Separator();
            ImGui::Text("MSAA \xe6\x8a\x97\xe9\x94\xaf\xe9\xbd\xbf");
            {
                const char* msaaItems[] = {"\xe5\x85\xb3\xe9\x97\xad", "2x", "4x"};
                int msaaValues[] = {1, 2, 4};
                int currentMsaaIdx = 0;
                uint32_t currentSamples = m_HDRFramebuffer->GetSpecification().Samples;
                for (int i = 0; i < 3; i++)
                {
                    if (currentSamples == static_cast<uint32_t>(msaaValues[i]))
                    {
                        currentMsaaIdx = i;
                        break;
                    }
                }
                if (ImGui::Combo("MSAA", &currentMsaaIdx, msaaItems, 3))
                {
                    FramebufferSpecification spec = m_HDRFramebuffer->GetSpecification();
                    spec.Samples = msaaValues[currentMsaaIdx];
                    m_HDRFramebuffer = Framebuffer::Create(spec);
                }
            }

            ImGui::Separator();
            ImGui::Text("\xe5\xa4\xa9\xe7\xa9\xba\xe7\x9b\x92");

            if (m_SceneRenderer.GetSkyboxSystem().HasSkybox())
            {
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "\xe5\xb7\xb2\xe5\x8a\xa0\xe8\xbd\xbd");
                if (ImGui::Button("\xe6\xb8\x85\xe9\x99\xa4\xe5\xa4\xa9\xe7\xa9\xba\xe7\x9b\x92"))
                    m_SceneRenderer.GetSkyboxSystem().ClearSkybox();
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
                        m_SceneRenderer.GetSkyboxSystem().LoadSkybox(faces);
                    else
                        ENGINE_WARN("Skybox needs: right/left/top/bottom/front/back{}", ext);
                }
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("right/left/top/bottom/front/back + .jpg/.png");

            // 物理设置
            ImGui::Separator();
            ImGui::Text("\xe7\x89\xa9\xe7\x90\x86\xe8\xae\xbe\xe7\xbd\xae");
            {
                const char* backendItems[] = {"\xe6\x89\x8b\xe5\x86\x99\xe7\x89\xa9\xe7\x90\x86", "Bullet3"};
                int currentBackend = static_cast<int>(m_ActiveScene->GetPhysicsBackend());
                if (ImGui::Combo("\xe7\x89\xa9\xe7\x90\x86\xe5\x90\x8e\xe7\xab\xaf", &currentBackend, backendItems, 2))
                {
                    m_ActiveScene->SetPhysicsBackend(static_cast<PhysicsBackend>(currentBackend));
                }
            }
            ImGui::Checkbox("\xe6\x98\xbe\xe7\xa4\xba\xe7\xa2\xb0\xe6\x92\x9e\xe4\xbd\x93", &m_ShowPhysicsColliders);

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

        // Mouse picking
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

        // Gizmos
        if (m_SelectedEntity && m_GizmoType != -1 && m_SelectedEntity.HasComponent<TransformComponent>())
        {
            const glm::mat4& cameraView = m_EditorCamera.GetViewMatrix();
            const glm::mat4& cameraProjection = m_EditorCamera.GetProjection();

            auto& tc = m_SelectedEntity.GetComponent<TransformComponent>();
            glm::mat4 transform = tc.GetTransform();

            ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
                                 static_cast<ImGuizmo::OPERATION>(m_GizmoType), ImGuizmo::LOCAL,
                                 glm::value_ptr(transform));

            if (ImGuizmo::IsUsing())
            {
                glm::vec3 translation, rotation, scale;
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(transform), glm::value_ptr(translation),
                                                      glm::value_ptr(rotation), glm::value_ptr(scale));

                glm::vec3 deltaRotation = glm::radians(rotation) - tc.Rotation;
                tc.Translation = translation;
                tc.Rotation += deltaRotation;
                tc.Scale = scale;
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
        m_ActiveScene = CreateRef<Scene>();
        m_ActiveScene->SetSceneRenderer(&m_SceneRenderer);
        m_ActiveScene->OnViewportResize(static_cast<uint32_t>(m_ViewportSize.x),
                                        static_cast<uint32_t>(m_ViewportSize.y));
        m_HierarchyPanel.SetContext(m_ActiveScene);
        m_SelectedEntity = {};
        m_HoveredEntity = {};
    }

    void EditorLayer::OpenScene()
    {
        std::string filepath = FileDialogs::OpenFile("*.scene", "\xe5\x9c\xba\xe6\x99\xaf\xe6\x96\x87\xe4\xbb\xb6");
        if (filepath.empty())
            return;

        auto newScene = CreateRef<Scene>();
        newScene->SetSceneRenderer(&m_SceneRenderer);
        EditorRenderSettings renderSettings;
        SceneSerializer serializer(newScene);
        if (!serializer.Deserialize(filepath, &renderSettings))
        {
            ENGINE_WARN("Failed to load scene from '{0}', keeping current scene", filepath);
            return;
        }

        m_ActiveScene = newScene;
        m_ActiveScene->OnViewportResize(static_cast<uint32_t>(m_ViewportSize.x),
                                        static_cast<uint32_t>(m_ViewportSize.y));
        m_HierarchyPanel.SetContext(m_ActiveScene);
        m_SelectedEntity = {};
        m_HoveredEntity = {};

        m_PostProcessingSettings = renderSettings.PostProcessing;
        m_ActiveScene->SetPhysicsBackend(static_cast<PhysicsBackend>(renderSettings.PhysicsBackend));

        if (renderSettings.MSAASamples != m_HDRFramebuffer->GetSpecification().Samples)
        {
            FramebufferSpecification spec = m_HDRFramebuffer->GetSpecification();
            spec.Samples = renderSettings.MSAASamples;
            m_HDRFramebuffer = Framebuffer::Create(spec);
        }
    }

    void EditorLayer::SaveScene()
    {
        std::string filepath = FileDialogs::SaveFile("*.scene", "\xe5\x9c\xba\xe6\x99\xaf\xe6\x96\x87\xe4\xbb\xb6");
        if (filepath.empty())
            return;

        EditorRenderSettings renderSettings;
        renderSettings.PostProcessing = m_PostProcessingSettings;
        renderSettings.MSAASamples = m_HDRFramebuffer->GetSpecification().Samples;
        renderSettings.PhysicsBackend = static_cast<int>(m_ActiveScene->GetPhysicsBackend());

        SceneSerializer serializer(m_ActiveScene);
        if (serializer.Serialize(filepath, renderSettings))
            ENGINE_INFO("Scene saved to '{0}'", filepath);
        else
            ENGINE_WARN("Failed to save scene to '{0}'", filepath);
    }

    void EditorLayer::OnScenePlay()
    {
        m_SceneState = SceneState::Play;

        // 深拷贝当前场景作为编辑器快照（ShadowSettings 保存在 Copy 的 fallback 中）
        m_EditorScene = Scene::Copy(m_ActiveScene);
        m_EditorScene->SetSceneRenderer(&m_SceneRenderer);

        m_ActiveScene->OnRuntimeStart();
    }

    void EditorLayer::OnSceneStop()
    {
        m_SceneState = SceneState::Edit;

        m_ActiveScene->OnRuntimeStop();

        // 恢复编辑器场景
        m_ActiveScene = m_EditorScene;
        m_EditorScene = nullptr;

        // 恢复 ShadowSettings（从备份的 fallback 恢复到 renderer）
        m_SceneRenderer.GetShadowSystem().GetSettings() = m_ActiveScene->GetShadowSettings();

        m_HierarchyPanel.SetContext(m_ActiveScene);
        m_SelectedEntity = {};
        m_HoveredEntity = {};
    }

} // namespace Engine
