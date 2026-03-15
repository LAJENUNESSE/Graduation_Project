#include "EditorLayer.h"

#include "Asset/AssetManager.h"
#include "Core/Application.h"
#include "Core/FileDialogs.h"
#include "Core/Log.h"
#include "EditorBootstrapper.h"
#include "EditorPanelCoordinator.h"
#include "EditorRenderController.h"
#include "EditorSceneSession.h"
#include "EditorSelectionGizmoController.h"
#include "EditorShell.h"
#include "EditorViewportController.h"
#include "Panels/RenderSettingsPanel.h"
#include "Renderer/Mesh.h"
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

        m_Boot = CreateScope<EditorBootstrapper>();
        m_Boot->Assemble();

        // 这些回调捕获 EditorLayer::this，必须在 Assemble 之后设置
        m_Boot->SetSceneOpenCallback([this](const std::string& path) { OpenScene(path); });
        m_Boot->SetMSAAChangedCallback([this](uint32_t samples) { m_Boot->RenderController().ApplyMSAASamples(samples); });

        BootstrapDefaultScene();

        // RenderSettingsPanel 初始 context（需要 m_ActiveScene，所以在 BootstrapDefaultScene 之后）
        m_Boot->GetRenderSettingsPanel().SetContext(
            &m_Boot->RenderController().GetSceneRenderer(),
            m_Boot->RenderController().GetPostProcessingSettings(),
            m_Boot->ViewportController().GetHDRFramebuffer(), m_ActiveScene,
            &m_Boot->ShowPhysicsColliders());

        ApplyActiveSceneContext(false);
    }

    void EditorLayer::OnDetach()
    {
        ENGINE_INFO("[EditorEvent] Detaching editor layer");
        if (m_Boot->SceneSession().IsPlaying())
            OnSceneStop();
        m_Boot->Teardown();
    }

    void EditorLayer::OnUpdate(Timestep ts)
    {
        m_Boot->ViewportController().OnUpdate(ts, *m_ActiveScene);
        AssetManager::Update(ts);
        m_Boot->RenderController().OnUpdate(ts, m_ActiveScene, m_Boot->SceneSession().GetState());
    }

    void EditorLayer::OnImGuiRender()
    {
        HandleShellActions(m_Boot->Shell().Draw(BuildShellState()));
        m_Boot->PanelCoordinator().RenderPanels();

        EditorViewportContext viewportContext = m_Boot->ViewportController().BeginViewportWindow();
        if (m_Boot->SceneSession().GetState() == SceneState::Edit)
        {
            m_Boot->SelectionGizmoController().UpdateHoveredEntity(
                viewportContext, m_Boot->ViewportController().GetPickingFramebuffer(), m_ActiveScene);
            m_Boot->SelectionGizmoController().RenderGizmos(
                viewportContext, m_Boot->ViewportController().GetCamera(), m_ActiveScene);
        }
        m_Boot->ViewportController().EndViewportWindow();
    }

    void EditorLayer::OnEvent(Event& event)
    {
        m_Boot->ViewportController().OnEvent(event);

        EventDispatcher dispatcher(event);
        dispatcher.Dispatch<KeyPressedEvent>(ENGINE_BIND_EVENT_FN(EditorLayer::OnKeyPressed));
        dispatcher.Dispatch<MouseButtonPressedEvent>(ENGINE_BIND_EVENT_FN(EditorLayer::OnMouseButtonPressed));
    }

    bool EditorLayer::OnKeyPressed(KeyPressedEvent& e)
    {
        HandleShellActions(m_Boot->Shell().OnKeyPressed(e, BuildShellState()));
        if (m_Boot->SceneSession().GetState() == SceneState::Edit)
            m_Boot->SelectionGizmoController().OnKeyPressed(e);
        return false;
    }

    bool EditorLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        if (m_Boot->SceneSession().GetState() == SceneState::Edit)
            m_Boot->SelectionGizmoController().OnMouseButtonPressed(
                e, m_Boot->ViewportController().GetContext(), m_ActiveScene);
        return false;
    }

    void EditorLayer::NewScene()
    {
        glm::vec2 renderSize = m_Boot->ViewportController().GetRenderSize();
        m_Boot->SceneSession().CreateNewScene(m_ActiveScene, static_cast<uint32_t>(renderSize.x),
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
        glm::vec2 renderSize = m_Boot->ViewportController().GetRenderSize();

        EditorRenderSettings renderSettings;
        if (!m_Boot->SceneSession().OpenSceneFromPath(m_ActiveScene, filepath, static_cast<uint32_t>(renderSize.x),
                                                      static_cast<uint32_t>(renderSize.y), &renderSettings))
        {
            return;
        }

        m_Boot->RenderController().ApplyRenderSettings(m_ActiveScene, renderSettings);
        ApplyActiveSceneContext(true);
    }

    void EditorLayer::SaveScene()
    {
        std::string filepath = FileDialogs::SaveFile("*.scene", "场景文件");
        if (filepath.empty())
            return;

        Ref<Scene> sceneToSave = m_Boot->SceneSession().GetSceneForSaving(m_ActiveScene);
        m_Boot->SceneSession().SaveSceneToPath(
            sceneToSave, filepath, m_Boot->RenderController().CollectRenderSettings(sceneToSave));
    }

    void EditorLayer::OnScenePlay()
    {
        m_Boot->SelectionGizmoController().ClearTransientState();
        m_Boot->SceneSession().BeginPlay(m_ActiveScene);
        ApplyActiveSceneContext(false);
    }

    void EditorLayer::OnSceneStop()
    {
        m_Boot->SceneSession().EndPlay(m_ActiveScene);
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
        if (actions.RequestUndo && m_Boot->SceneSession().GetState() == SceneState::Edit)
            m_Boot->GetCommandHistory().UndoCommand();
        if (actions.RequestRedo && m_Boot->SceneSession().GetState() == SceneState::Edit)
            m_Boot->GetCommandHistory().RedoCommand();
        if (actions.ToggleStatsPanel)
            m_Boot->PanelCoordinator().ToggleStatsPanelVisible();
        if (actions.RequestCloseApplication)
            Application::Get().Close();
    }

    EditorShellState EditorLayer::BuildShellState() const
    {
        EditorShellState state;
        const bool allowHistoryActions = m_Boot->SceneSession().GetState() == SceneState::Edit;
        state.CurrentSceneState = m_Boot->SceneSession().GetState();
        state.CanUndo = allowHistoryActions && m_Boot->GetCommandHistory().CanUndo();
        state.CanRedo = allowHistoryActions && m_Boot->GetCommandHistory().CanRedo();
        state.UndoDescription = allowHistoryActions ? m_Boot->GetCommandHistory().GetUndoDescription() : "";
        state.RedoDescription = allowHistoryActions ? m_Boot->GetCommandHistory().GetRedoDescription() : "";
        state.ShowStatsPanel = m_Boot->PanelCoordinator().IsStatsPanelVisible();
        return state;
    }

    void EditorLayer::BootstrapDefaultScene()
    {
        m_ActiveScene = CreateRef<Scene>();
        m_ActiveScene->SetSceneRenderer(&m_Boot->RenderController().GetSceneRenderer());

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

        m_Boot->SceneSession().SetEditorScene(m_ActiveScene);
    }

    void EditorLayer::ApplyActiveSceneContext(bool clearCommandHistory)
    {
        m_Boot->PanelCoordinator().ApplyScene(m_ActiveScene, clearCommandHistory);
        m_Boot->SelectionGizmoController().ClearTransientState();
    }

} // namespace Engine
