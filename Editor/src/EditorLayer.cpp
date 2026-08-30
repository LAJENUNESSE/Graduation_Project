#include "EditorLayer.h"

#include "Asset/AssetManager.h"
#include "Asset/PathUtils.h"
#include "Core/Application.h"
#include "Core/CrashHandler.h"
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

#include <filesystem>

namespace Engine
{
    void EditorLayer::RestoreEditorRenderSettingsSnapshot()
    {
        if (!m_PrePlayRenderSettings || !m_ActiveScene)
            return;

        m_Boot->RenderController().ApplyRenderSettings(m_ActiveScene, *m_PrePlayRenderSettings);
        m_PrePlayRenderSettings.reset();
    }

    EditorLayer::EditorLayer() : Layer("EditorLayer") {}

    EditorLayer::~EditorLayer() = default;

    void EditorLayer::OnAttach()
    {
        ENGINE_INFO("EditorLayer OnAttach");

        m_Boot = CreateScope<EditorBootstrapper>();
        m_Boot->Assemble();

        // 这些回调捕获 EditorLayer::this，必须在 Assemble 之后设置
        m_Boot->SetSceneOpenCallback([this](const std::string& path) { OpenScene(path); });
        m_Boot->SetScriptOpenCallback([this](const std::string& path) { m_Boot->PanelCoordinator().OpenScript(path); });
        m_Boot->SetMSAAChangedCallback([this](uint32_t samples)
                                       { m_Boot->RenderController().ApplyMSAASamples(samples); });

        BootstrapDefaultScene();

        // 测试辅助：ENGINE_EDITOR_OPEN_SCENE 指定场景直接打开（自动化验证用，跳过 Boot 界面交互）
        if (const char* sceneOverride = std::getenv("ENGINE_EDITOR_OPEN_SCENE"))
        {
            const std::filesystem::path resolved = PathUtils::GetProjectRoot() / sceneOverride;
            if (std::filesystem::exists(resolved))
                OpenScene(resolved.string());
            else
                ENGINE_WARN("[Editor] ENGINE_EDITOR_OPEN_SCENE='{0}' 不存在，使用默认场景", sceneOverride);
        }

        // RenderSettingsPanel 初始 context（需要 m_ActiveScene，所以在 BootstrapDefaultScene 之后）
        m_Boot->GetRenderSettingsPanel().SetContext(&m_Boot->RenderController().GetSceneRenderer(),
                                                    m_Boot->RenderController().GetPostProcessingSettings(),
                                                    m_Boot->ViewportController().GetHDRFramebuffer(), m_ActiveScene,
                                                    &m_Boot->ShowPhysicsColliders(), &m_Boot->ShowMeshSDFBounds());

        ApplyActiveSceneContext(false);

        // 关闭拦截器
        Application::Get().SetCloseInterceptor([this]() { return PromptSaveIfDirty(); });

        // CommandHistory → MarkDirty 桥接
        m_Boot->SetModifiedCallback(
            [this]()
            {
                m_Boot->SceneSession().MarkDirty();
                UpdateWindowTitle();
            });

        // 崩溃紧急保存
        CrashHandler::SetEmergencySaveCallback([this]() { PerformAutosave(); });

        // 启动时检查崩溃恢复
        CheckAndPromptRestore();
        UpdateWindowTitle();
    }

    void EditorLayer::OnDetach()
    {
        ENGINE_INFO("[EditorEvent] Detaching editor layer");
        Application::Get().SetCloseInterceptor(nullptr);
        CrashHandler::SetEmergencySaveCallback(nullptr);
        CleanupAutosave();
        if (m_Boot->SceneSession().IsPlaying())
            OnSceneStop();
        m_Boot->Teardown();
    }

    void EditorLayer::OnUpdate(Timestep ts)
    {
        // 自动保存计时
        if (m_Boot->SceneSession().IsDirty())
        {
            m_AutosaveTimer += ts;
            if (m_AutosaveTimer >= AutosaveInterval)
            {
                PerformAutosave();
                m_AutosaveTimer = 0.0f;
            }
        }
        else
        {
            // 仅在连续脏状态下累计，避免“清空后再次修改立即触发自动保存”
            m_AutosaveTimer = 0.0f;
        }

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
            m_Boot->SelectionGizmoController().RenderGizmos(viewportContext, m_Boot->ViewportController().GetCamera(),
                                                            m_ActiveScene);
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
            m_Boot->SelectionGizmoController().OnMouseButtonPressed(e, m_Boot->ViewportController().GetContext(),
                                                                    m_ActiveScene);
        return false;
    }

    void EditorLayer::NewScene()
    {
        if (!PromptSaveIfDirty())
            return;

        if (m_Boot->SceneSession().IsPlaying())
            OnSceneStop();
        glm::vec2 renderSize = m_Boot->ViewportController().GetRenderSize();
        m_Boot->SceneSession().CreateNewScene(m_ActiveScene, static_cast<uint32_t>(renderSize.x),
                                              static_cast<uint32_t>(renderSize.y));

        SyncCommandHistorySuspension();
        ApplyActiveSceneContext(true);
        UpdateWindowTitle();
    }

    void EditorLayer::OpenScene()
    {
        std::string filepath = FileDialogs::OpenFile("*.scene", "场景文件");
        if (!filepath.empty())
            OpenScene(filepath);
    }

    void EditorLayer::OpenScene(const std::string& filepath)
    {
        if (!PromptSaveIfDirty())
            return;

        if (m_Boot->SceneSession().IsPlaying())
            OnSceneStop();
        glm::vec2 renderSize = m_Boot->ViewportController().GetRenderSize();

        EditorRenderSettings renderSettings;
        const bool           opened =
            m_Boot->SceneSession().OpenSceneFromPath(m_ActiveScene, filepath, static_cast<uint32_t>(renderSize.x),
                                                     static_cast<uint32_t>(renderSize.y), &renderSettings);
        SyncCommandHistorySuspension();
        if (!opened)
        {
            return;
        }

        m_Boot->RenderController().ApplyRenderSettings(m_ActiveScene, renderSettings);
        ApplyActiveSceneContext(true);
        UpdateWindowTitle();
    }

    bool EditorLayer::SaveSceneQuick()
    {
        if (!m_Boot->SceneSession().IsDirty())
            return true;

        auto& session = m_Boot->SceneSession();
        if (!session.HasScenePath())
        {
            SaveSceneAs();
            return !session.IsDirty();
        }

        Ref<Scene> sceneToSave = session.GetSceneForSaving(m_ActiveScene);
        bool       ok          = session.SaveSceneToPath(sceneToSave, session.GetCurrentScenePath(),
                                                         m_Boot->RenderController().CollectRenderSettings(sceneToSave));
        if (ok)
        {
            session.ClearDirty();
            m_AutosaveTimer = 0.0f;
            CleanupAutosave();
            UpdateWindowTitle();
        }
        return ok;
    }

    void EditorLayer::SaveSceneAs()
    {
        std::string filepath = FileDialogs::SaveFile("*.scene", "\xe5\x9c\xba\xe6\x99\xaf\xe6\x96\x87\xe4\xbb\xb6");
        if (filepath.empty())
            return;

        Ref<Scene> sceneToSave = m_Boot->SceneSession().GetSceneForSaving(m_ActiveScene);
        bool       ok          = m_Boot->SceneSession().SaveSceneToPath(sceneToSave, filepath,
                                                                        m_Boot->RenderController().CollectRenderSettings(sceneToSave));
        if (ok)
        {
            m_Boot->SceneSession().SetCurrentScenePath(filepath);
            m_Boot->SceneSession().ClearDirty();
            m_AutosaveTimer = 0.0f;
            CleanupAutosave();
            UpdateWindowTitle();
        }
    }
    void EditorLayer::OnScenePlay()
    {
        if (m_Boot->SceneSession().GetState() != SceneState::Edit || !m_ActiveScene)
            return;

        m_PrePlayRenderSettings = m_Boot->RenderController().CollectRenderSettings(m_ActiveScene);
        m_Boot->SelectionGizmoController().ClearTransientState();
        m_Boot->SceneSession().BeginPlay(m_ActiveScene);
        SyncCommandHistorySuspension();
        ApplyActiveSceneContext(false);
    }

    void EditorLayer::OnSceneStop()
    {
        if (!m_Boot->SceneSession().IsPlaying())
            return;

        m_Boot->SceneSession().EndPlay(m_ActiveScene);
        SyncCommandHistorySuspension();
        RestoreEditorRenderSettingsSnapshot();

        // Play 期间 viewport resize 只作用于 runtime scene，需同步回 editor scene
        glm::vec2 renderSize = m_Boot->ViewportController().GetRenderSize();
        if (m_ActiveScene && renderSize.x > 0 && renderSize.y > 0)
            m_ActiveScene->OnViewportResize(static_cast<uint32_t>(renderSize.x), static_cast<uint32_t>(renderSize.y));

        ApplyActiveSceneContext(false);
    }

    void EditorLayer::HandleShellActions(const EditorShellActions& actions)
    {
        if (actions.RequestNewScene)
            NewScene();
        if (actions.RequestOpenScene)
            OpenScene();
        if (actions.RequestSaveSceneQuick)
            SaveSceneQuick();
        if (actions.RequestSaveScene)
            SaveSceneAs();
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
        if (actions.ToggleMemoryPanel)
            m_Boot->PanelCoordinator().ToggleMemoryPanelVisible();
        if (actions.ToggleScriptEditorPanel)
            m_Boot->PanelCoordinator().ToggleScriptEditorVisible();
        if (actions.ToggleFpsOverlay)
            m_Boot->ViewportController().ToggleFpsOverlay();
        if (actions.ToggleLayoutLock)
            m_LayoutLocked = !m_LayoutLocked;
        if (actions.RequestResetLayout)
            m_Boot->Shell().RequestResetLayout();
        if (actions.RequestCloseApplication)
        {
            if (PromptSaveIfDirty())
                Application::Get().Close();
        }
    }

    EditorShellState EditorLayer::BuildShellState() const
    {
        EditorShellState state;
        const bool       allowHistoryActions = m_Boot->SceneSession().GetState() == SceneState::Edit;
        state.CurrentSceneState              = m_Boot->SceneSession().GetState();
        state.CanSave                        = allowHistoryActions;
        state.CanUndo                        = allowHistoryActions && m_Boot->GetCommandHistory().CanUndo();
        state.CanRedo                        = allowHistoryActions && m_Boot->GetCommandHistory().CanRedo();
        state.UndoDescription       = allowHistoryActions ? m_Boot->GetCommandHistory().GetUndoDescription() : "";
        state.RedoDescription       = allowHistoryActions ? m_Boot->GetCommandHistory().GetRedoDescription() : "";
        state.ShowStatsPanel        = m_Boot->PanelCoordinator().IsStatsPanelVisible();
        state.ShowMemoryPanel       = m_Boot->PanelCoordinator().IsMemoryPanelVisible();
        state.ShowScriptEditorPanel = m_Boot->PanelCoordinator().IsScriptEditorVisible();
        state.IsDirty               = m_Boot->SceneSession().IsDirty();
        state.ShowFpsOverlay        = m_Boot->ViewportController().IsShowFpsOverlay();
        state.IsLayoutLocked        = m_LayoutLocked;
        return state;
    }

    void EditorLayer::BootstrapDefaultScene()
    {
        m_ActiveScene = CreateRef<Scene>();
        m_ActiveScene->SetSceneRenderer(&m_Boot->RenderController().GetSceneRenderer());

        Entity cubeEntity      = m_ActiveScene->CreateEntity("Cube");
        auto&  meshRenderer    = cubeEntity.AddComponent<MeshRendererComponent>();
        meshRenderer.Type      = MeshType::Cube;
        meshRenderer.MeshAsset = AssetManager::Load<Mesh>("builtin:Cube");
        meshRenderer.Color     = {0.8f, 0.2f, 0.3f, 1.0f};

        Entity lightEntity      = m_ActiveScene->CreateEntity("方向光");
        auto&  light            = lightEntity.AddComponent<LightComponent>();
        light.Type              = LightComponent::LightType::Directional;
        light.Color             = {1.0f, 0.95f, 0.9f};
        auto& lightTransform    = lightEntity.GetComponent<TransformComponent>();
        lightTransform.Rotation = {glm::radians(-45.0f), glm::radians(30.0f), 0.0f};

        m_Boot->SceneSession().SetEditorScene(m_ActiveScene);
    }

    void EditorLayer::ApplyActiveSceneContext(bool clearCommandHistory)
    {
        m_Boot->PanelCoordinator().ApplyScene(m_ActiveScene, clearCommandHistory);
        m_Boot->PanelCoordinator().SetPanelsReadOnly(m_Boot->SceneSession().GetState() == SceneState::Play);
        m_Boot->SelectionGizmoController().ClearTransientState();
    }

    void EditorLayer::SyncCommandHistorySuspension()
    {
        m_Boot->GetCommandHistory().SetSuspended(m_Boot->SceneSession().GetState() == SceneState::Play);
    }

    bool EditorLayer::PromptSaveIfDirty()
    {
        if (!m_Boot->SceneSession().IsDirty())
            return true;

        auto result = FileDialogs::ShowYesNoCancelBox(
            "\xe4\xbf\x9d\xe5\xad\x98\xe6\x8f\x90\xe7\xa4\xba",
            "\xe5\xbd\x93\xe5\x89\x8d\xe5\x9c\xba\xe6\x99\xaf\xe5\xb7\xb2\xe4\xbf\xae\xe6\x94\xb9\xe4\xbd\x86\xe5\xb0"
            "\x9a\xe6\x9c\xaa\xe4\xbf\x9d\xe5\xad\x98\xe3\x80\x82\n\xe6\x98\xaf\xe5\x90\xa6\xe4\xbf\x9d\xe5\xad\x98\xe6"
            "\x9b\xb4\xe6\x94\xb9\xef\xbc\x9f");
        // "保存提示" / "当前场景已修改但尚未保存。\n是否保存更改？"

        switch (result)
        {
        case FileDialogs::MessageBoxResult::Yes:
            return SaveSceneQuick();
        case FileDialogs::MessageBoxResult::No:
            return true;
        case FileDialogs::MessageBoxResult::Cancel:
        default:
            return false;
        }
    }

    void EditorLayer::UpdateWindowTitle()
    {
        auto&       session = m_Boot->SceneSession();
        std::string title   = "Game Engine";

        if (session.HasScenePath())
        {
            std::filesystem::path p(session.GetCurrentScenePath());
            title += " - " + p.filename().string();
        }
        else
        {
            title += " - Untitled";
        }

        if (session.IsDirty())
            title += " *";

        Application::Get().GetWindow().SetTitle(title);
    }

    void EditorLayer::PerformAutosave()
    {
        if (m_Boot->SceneSession().GetState() != SceneState::Edit)
            return;

        std::error_code ec;
        auto            dir = PathUtils::GetProjectRoot() / ".autosave";
        std::filesystem::create_directories(dir, ec);
        if (ec)
        {
            ENGINE_WARN("[Autosave] Failed to create directory: {0}", ec.message());
            return;
        }

        auto            path  = (dir / "autosave.scene").string();
        Ref<Scene>      scene = m_Boot->SceneSession().GetSceneForSaving(m_ActiveScene);
        SceneSerializer serializer(scene);
        if (serializer.Serialize(path, m_Boot->RenderController().CollectRenderSettings(scene)))
            ENGINE_INFO("[Autosave] Saved to {0}", path);
        else
            ENGINE_WARN("[Autosave] Failed to save");
    }

    void EditorLayer::CleanupAutosave()
    {
        std::error_code ec;
        std::filesystem::remove_all(PathUtils::GetProjectRoot() / ".autosave", ec);
    }

    void EditorLayer::CheckAndPromptRestore()
    {
        auto path = (PathUtils::GetProjectRoot() / ".autosave" / "autosave.scene").string();
        if (!std::filesystem::exists(path))
            return;

        auto result = FileDialogs::ShowYesNoCancelBox(
            "\xe5\xb4\xa9\xe6\xba\x83\xe6\x81\xa2\xe5\xa4\x8d",
            "\xe6\xa3\x80\xe6\xb5\x8b\xe5\x88\xb0\xe4\xb8\x8a\xe6\xac\xa1\xe7\xbc\x96\xe8\xbe\x91\xe5\x99\xa8\xe5\xbc"
            "\x82\xe5\xb8\xb8\xe9\x80\x80\xe5\x87\xba\xe6\x97\xb6\xe7\x9a\x84\xe8\x87\xaa\xe5\x8a\xa8\xe4\xbf\x9d\xe5"
            "\xad\x98\xe6\x96\x87\xe4\xbb\xb6\xe3\x80\x82\n\xe6\x98\xaf\xe5\x90\xa6\xe6\x81\xa2\xe5\xa4\x8d\xef\xbc"
            "\x9f");
        // "崩溃恢复" / "检测到上次编辑器异常退出时的自动保存文件。\n是否恢复？"

        if (result == FileDialogs::MessageBoxResult::Yes)
        {
            glm::vec2            sz = m_Boot->ViewportController().GetRenderSize();
            EditorRenderSettings rs;
            if (m_Boot->SceneSession().OpenSceneFromPath(m_ActiveScene, path, static_cast<uint32_t>(sz.x),
                                                         static_cast<uint32_t>(sz.y), &rs))
            {
                m_Boot->RenderController().ApplyRenderSettings(m_ActiveScene, rs);
                m_Boot->SceneSession().MarkDirty();
                m_Boot->SceneSession().SetCurrentScenePath("");
                ApplyActiveSceneContext(true);
                ENGINE_INFO("[Autosave] Restored from autosave");
                CleanupAutosave();
            }
            else
            {
                ENGINE_WARN("[Autosave] Failed to restore (file kept for manual recovery), loading default scene");
            }
        }
        else
        {
            CleanupAutosave();
        }
    }

} // namespace Engine
