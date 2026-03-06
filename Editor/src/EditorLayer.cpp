#include "EditorLayer.h"

#include "Asset/AssetManager.h"
#include "Core/FileDialogs.h"
#include "Core/Log.h"
#include "Debug/PerformanceMonitor.h"
#include "Debug/ProfileTimer.h"
#include "Renderer/Mesh.h"
#include "Renderer/Renderer.h"
#include "Scene/Components.h"

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
        m_SceneSession.Initialize(&m_SceneRenderer);
        m_SceneRenderer.SetPostProcessing(&m_PostProcessing, &m_PostProcessingSettings);
        m_SceneRenderer.SetDebugDrawCallback([this]() {
            if (m_ShowPhysicsColliders)
                m_PhysicsDebugDraw.DrawColliders(m_ActiveScene->GetRegistry(), m_ViewportController.GetCamera());
        });
        SyncHDRFramebufferBindings();

        BootstrapDefaultScene();
        ConfigureEditorPanels();
        ApplyActiveSceneContext(false);
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
        m_SelectionGizmoController.UpdateHoveredEntity(viewportContext,
                                                       m_ViewportController.GetHDRFramebuffer(),
                                                       m_ActiveScene);
        m_SelectionGizmoController.RenderGizmos(viewportContext,
                                                m_ViewportController.GetCamera(),
                                                m_ActiveScene);
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
        m_SelectionGizmoController.OnKeyPressed(e);
        return false;
    }

    bool EditorLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        m_SelectionGizmoController.OnMouseButtonPressed(e, m_ViewportController.GetContext(), m_ActiveScene);
        return false;
    }

    void EditorLayer::NewScene()
    {
        const auto& viewportContext = m_ViewportController.GetContext();
        m_SceneSession.CreateNewScene(
            m_ActiveScene,
            static_cast<uint32_t>(viewportContext.Size.x),
            static_cast<uint32_t>(viewportContext.Size.y));

        ApplyActiveSceneContext(true);
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

        ApplyRenderSettings(renderSettings);
        ApplyActiveSceneContext(true);
    }

    void EditorLayer::SaveScene()
    {
        std::string filepath = FileDialogs::SaveFile("*.scene", "场景文件");
        if (filepath.empty())
            return;

        m_SceneSession.SaveSceneToPath(m_ActiveScene, filepath, CollectRenderSettings());
    }

    void EditorLayer::OnScenePlay()
    {
        m_SceneSession.BeginPlay(m_ActiveScene);
    }

    void EditorLayer::OnSceneStop()
    {
        m_SceneSession.EndPlay(m_ActiveScene);
        ApplyActiveSceneContext(false);
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

    void EditorLayer::BootstrapDefaultScene()
    {
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
    }

    void EditorLayer::ConfigureEditorPanels()
    {
        m_HierarchyPanel.SetCommandHistory(&m_CommandHistory);
        m_AssetBrowserPanel.SetSceneOpenCallback([this](const std::string& path) {
            OpenScene(path);
        });

        m_RenderSettingsPanel.SetContext(&m_SceneRenderer, &m_PostProcessingSettings,
                                         m_ViewportController.GetHDRFramebuffer(), m_ActiveScene,
                                         &m_ShowPhysicsColliders);
        m_RenderSettingsPanel.SetMSAAChangedCallback([this](uint32_t samples) {
            m_ViewportController.ApplyMSAASamples(samples);
            SyncHDRFramebufferBindings();
        });

        m_PanelCoordinator.Initialize(
            &m_HierarchyPanel,
            &m_PropertiesPanel,
            &m_ConsolePanel,
            &m_AssetBrowserPanel,
            &m_RenderSettingsPanel,
            &m_CommandHistory);
        m_SelectionGizmoController.Initialize(&m_PanelCoordinator, &m_CommandHistory);
    }

    void EditorLayer::ApplyActiveSceneContext(bool clearCommandHistory)
    {
        m_PanelCoordinator.ApplyScene(m_ActiveScene, clearCommandHistory);
        m_SelectionGizmoController.ClearTransientState();
    }

    void EditorLayer::SyncHDRFramebufferBindings()
    {
        m_SceneRenderer.SetHDRFramebuffer(m_ViewportController.GetHDRFramebuffer());
        m_PanelCoordinator.SetHDRFramebuffer(m_ViewportController.GetHDRFramebuffer());
    }

    void EditorLayer::ApplyRenderSettings(const EditorRenderSettings& renderSettings)
    {
        m_PostProcessingSettings = renderSettings.PostProcessing;
        m_ActiveScene->SetPhysicsBackend(static_cast<PhysicsBackend>(renderSettings.PhysicsBackend));

        m_SceneRenderer.GetSSAOEnabled() = renderSettings.SSAOEnabled;
        m_SceneRenderer.GetSSAORadius() = renderSettings.SSAORadius;
        m_SceneRenderer.GetSSAOBias() = renderSettings.SSAOBias;
        m_SceneRenderer.GetSSAOKernelSize() = renderSettings.SSAOKernelSize;
        m_SceneRenderer.GetSSAOIntensity() = renderSettings.SSAOIntensity;

        if (renderSettings.MSAASamples != m_ViewportController.GetHDRFramebuffer()->GetSpecification().Samples)
            m_ViewportController.ApplyMSAASamples(renderSettings.MSAASamples);

        SyncHDRFramebufferBindings();
    }

    EditorRenderSettings EditorLayer::CollectRenderSettings()
    {
        EditorRenderSettings renderSettings;
        renderSettings.PostProcessing = m_PostProcessingSettings;
        renderSettings.MSAASamples = m_ViewportController.GetHDRFramebuffer()->GetSpecification().Samples;
        renderSettings.PhysicsBackend = static_cast<int>(m_ActiveScene->GetPhysicsBackend());
        renderSettings.SSAOEnabled = m_SceneRenderer.GetSSAOEnabled();
        renderSettings.SSAORadius = m_SceneRenderer.GetSSAORadius();
        renderSettings.SSAOBias = m_SceneRenderer.GetSSAOBias();
        renderSettings.SSAOKernelSize = m_SceneRenderer.GetSSAOKernelSize();
        renderSettings.SSAOIntensity = m_SceneRenderer.GetSSAOIntensity();
        return renderSettings;
    }

} // namespace Engine

