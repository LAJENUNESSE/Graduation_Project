#include "EditorLayer.h"

#include "Asset/AssetManager.h"
#include "Core/Application.h"
#include "Core/FileDialogs.h"
#include "Core/Log.h"
#include "EditorPanelCoordinator.h"
#include "EditorRenderController.h"
#include "EditorSceneSession.h"
#include "EditorSelectionGizmoController.h"
#include "EditorShell.h"
#include "EditorViewportController.h"
#include "Panels/AssetBrowserPanel.h"
#include "Panels/ConsolePanel.h"
#include "Panels/PropertiesPanel.h"
#include "Panels/RenderSettingsPanel.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Physics/PhysicsDebugDraw.h"
#include "Renderer/Mesh.h"
#include "Renderer/PostProcessing.h"
#include "Renderer/SceneRenderer.h"
#include "Scene/Components.h"
#include "UndoSystem.h"

namespace Engine
{

    EditorLayer::EditorLayer() : Layer("EditorLayer") {}

    EditorLayer::~EditorLayer() = default;

    void EditorLayer::OnAttach()
    {
        ENGINE_INFO("EditorLayer OnAttach");

        m_CommandHistory = CreateScope<CommandHistory>();

        m_HierarchyPanel = CreateScope<SceneHierarchyPanel>();
        m_PropertiesPanel = CreateScope<PropertiesPanel>();
        m_ConsolePanel = CreateScope<ConsolePanel>();
        m_AssetBrowserPanel = CreateScope<AssetBrowserPanel>();
        m_RenderSettingsPanel = CreateScope<RenderSettingsPanel>();

        m_SceneRenderer = CreateScope<SceneRenderer>();
        m_PostProcessing = CreateScope<PostProcessing>();
        m_PostProcessingSettings = CreateScope<PostProcessingSettings>();
        m_PhysicsDebugDraw = CreateScope<PhysicsDebugDraw>();

        m_SceneSession = CreateScope<EditorSceneSession>();
        m_PanelCoordinator = CreateScope<EditorPanelCoordinator>();
        m_SelectionGizmoController = CreateScope<EditorSelectionGizmoController>();
        m_EditorShell = CreateScope<EditorShell>();
        m_ViewportController = CreateScope<EditorViewportController>();
        m_RenderController = CreateScope<EditorRenderController>();

        m_ViewportController->Initialize();
        m_RenderController->Initialize(m_SceneRenderer.get(), m_PostProcessing.get(), m_PostProcessingSettings.get(),
                                       m_ViewportController.get(), m_PanelCoordinator.get(), m_PhysicsDebugDraw.get(),
                                       &m_ShowPhysicsColliders);
        m_RenderController->Attach();
        m_SceneSession->Initialize(&m_RenderController->GetSceneRenderer());

        BootstrapDefaultScene();
        ConfigureEditorPanels();
        ApplyActiveSceneContext(false);
        m_ConsolePanel->RegisterSink();
    }

    void EditorLayer::OnDetach()
    {
        ENGINE_INFO("[EditorEvent] Detaching editor layer");
        if (m_SceneSession && m_SceneSession->IsPlaying())
            OnSceneStop();
        if (m_ConsolePanel)
            m_ConsolePanel->UnregisterSink();
        if (m_RenderController)
            m_RenderController->Detach();
    }

    void EditorLayer::OnUpdate(Timestep ts)
    {
        m_ViewportController->OnUpdate(ts, *m_ActiveScene);
        AssetManager::Update(ts);
        m_RenderController->OnUpdate(ts, m_ActiveScene, m_SceneSession->GetState());
    }

    void EditorLayer::OnImGuiRender()
    {
        HandleShellActions(m_EditorShell->Draw(BuildShellState()));
        m_PanelCoordinator->RenderPanels();

        EditorViewportContext viewportContext = m_ViewportController->BeginViewportWindow();
        if (m_SceneSession->GetState() == SceneState::Edit)
        {
            m_SelectionGizmoController->UpdateHoveredEntity(
                viewportContext, m_ViewportController->GetPickingFramebuffer(), m_ActiveScene);
            m_SelectionGizmoController->RenderGizmos(viewportContext, m_ViewportController->GetCamera(), m_ActiveScene);
        }
        m_ViewportController->EndViewportWindow();
    }

    void EditorLayer::OnEvent(Event& event)
    {
        m_ViewportController->OnEvent(event);

        EventDispatcher dispatcher(event);
        dispatcher.Dispatch<KeyPressedEvent>(ENGINE_BIND_EVENT_FN(EditorLayer::OnKeyPressed));
        dispatcher.Dispatch<MouseButtonPressedEvent>(ENGINE_BIND_EVENT_FN(EditorLayer::OnMouseButtonPressed));
    }

    bool EditorLayer::OnKeyPressed(KeyPressedEvent& e)
    {
        HandleShellActions(m_EditorShell->OnKeyPressed(e, BuildShellState()));
        if (m_SceneSession->GetState() == SceneState::Edit)
            m_SelectionGizmoController->OnKeyPressed(e);
        return false;
    }

    bool EditorLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        if (m_SceneSession->GetState() == SceneState::Edit)
            m_SelectionGizmoController->OnMouseButtonPressed(e, m_ViewportController->GetContext(), m_ActiveScene);
        return false;
    }

    void EditorLayer::NewScene()
    {
        glm::vec2 renderSize = m_ViewportController->GetRenderSize();
        m_SceneSession->CreateNewScene(m_ActiveScene, static_cast<uint32_t>(renderSize.x),
                                       static_cast<uint32_t>(renderSize.y));

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
        glm::vec2 renderSize = m_ViewportController->GetRenderSize();

        EditorRenderSettings renderSettings;
        if (!m_SceneSession->OpenSceneFromPath(m_ActiveScene, filepath, static_cast<uint32_t>(renderSize.x),
                                               static_cast<uint32_t>(renderSize.y), &renderSettings))
        {
            return;
        }

        m_RenderController->ApplyRenderSettings(m_ActiveScene, renderSettings);
        ApplyActiveSceneContext(true);
    }

    void EditorLayer::SaveScene()
    {
        std::string filepath = FileDialogs::SaveFile("*.scene", "场景文件");
        if (filepath.empty())
            return;

        Ref<Scene> sceneToSave = m_SceneSession->GetSceneForSaving(m_ActiveScene);
        m_SceneSession->SaveSceneToPath(sceneToSave, filepath, m_RenderController->CollectRenderSettings(sceneToSave));
    }

    void EditorLayer::OnScenePlay()
    {
        m_SelectionGizmoController->ClearTransientState();
        m_SceneSession->BeginPlay(m_ActiveScene);
        ApplyActiveSceneContext(false);
    }

    void EditorLayer::OnSceneStop()
    {
        m_SceneSession->EndPlay(m_ActiveScene);
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
        if (actions.RequestUndo && m_SceneSession->GetState() == SceneState::Edit)
            m_CommandHistory->UndoCommand();
        if (actions.RequestRedo && m_SceneSession->GetState() == SceneState::Edit)
            m_CommandHistory->RedoCommand();
        if (actions.ToggleStatsPanel)
            m_PanelCoordinator->ToggleStatsPanelVisible();
        if (actions.RequestCloseApplication)
            Application::Get().Close();
    }

    EditorShellState EditorLayer::BuildShellState() const
    {
        EditorShellState state;
        const bool allowHistoryActions = m_SceneSession->GetState() == SceneState::Edit;
        state.CurrentSceneState = m_SceneSession->GetState();
        state.CanUndo = allowHistoryActions && m_CommandHistory->CanUndo();
        state.CanRedo = allowHistoryActions && m_CommandHistory->CanRedo();
        state.UndoDescription = allowHistoryActions ? m_CommandHistory->GetUndoDescription() : "";
        state.RedoDescription = allowHistoryActions ? m_CommandHistory->GetRedoDescription() : "";
        state.ShowStatsPanel = m_PanelCoordinator->IsStatsPanelVisible();
        return state;
    }

    void EditorLayer::BootstrapDefaultScene()
    {
        m_ActiveScene = CreateRef<Scene>();
        m_ActiveScene->SetSceneRenderer(&m_RenderController->GetSceneRenderer());

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

        m_SceneSession->SetEditorScene(m_ActiveScene);
    }

    void EditorLayer::ConfigureEditorPanels()
    {
        m_HierarchyPanel->SetCommandHistory(m_CommandHistory.get());
        m_AssetBrowserPanel->SetSceneOpenCallback([this](const std::string& path) { OpenScene(path); });

        m_RenderSettingsPanel->SetContext(
            &m_RenderController->GetSceneRenderer(), m_RenderController->GetPostProcessingSettings(),
            m_ViewportController->GetHDRFramebuffer(), m_ActiveScene, &m_ShowPhysicsColliders);
        m_RenderSettingsPanel->SetMSAAChangedCallback([this](uint32_t samples)
                                                      { m_RenderController->ApplyMSAASamples(samples); });

        m_PanelCoordinator->Initialize(m_HierarchyPanel.get(), m_PropertiesPanel.get(), m_ConsolePanel.get(),
                                       m_AssetBrowserPanel.get(), m_RenderSettingsPanel.get(), m_CommandHistory.get());
        m_SelectionGizmoController->Initialize(m_PanelCoordinator.get(), m_CommandHistory.get());
    }

    void EditorLayer::ApplyActiveSceneContext(bool clearCommandHistory)
    {
        m_PanelCoordinator->ApplyScene(m_ActiveScene, clearCommandHistory);
        m_SelectionGizmoController->ClearTransientState();
    }

} // namespace Engine
