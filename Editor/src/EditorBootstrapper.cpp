#include "EditorBootstrapper.h"

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
#include "Renderer/PostProcessing.h"
#include "Renderer/SceneRenderer.h"
#include "UndoSystem.h"

namespace Engine
{

    EditorBootstrapper::EditorBootstrapper() = default;

    EditorBootstrapper::~EditorBootstrapper() = default;

    void EditorBootstrapper::Assemble()
    {
        // 基础设施
        m_CommandHistory = CreateScope<CommandHistory>();
        m_SceneRenderer = CreateScope<SceneRenderer>();
        m_PostProcessing = CreateScope<PostProcessing>();
        m_PostProcessingSettings = CreateScope<PostProcessingSettings>();
        m_PhysicsDebugDraw = CreateScope<PhysicsDebugDraw>();

        // 面板
        m_HierarchyPanel = CreateScope<SceneHierarchyPanel>();
        m_PropertiesPanel = CreateScope<PropertiesPanel>();
        m_ConsolePanel = CreateScope<ConsolePanel>();
        m_AssetBrowserPanel = CreateScope<AssetBrowserPanel>();
        m_RenderSettingsPanel = CreateScope<RenderSettingsPanel>();

        // 控制器
        m_SceneSession = CreateScope<EditorSceneSession>(*m_SceneRenderer);
        m_PanelCoordinator = CreateScope<EditorPanelCoordinator>();
        m_SelectionGizmoController = CreateScope<EditorSelectionGizmoController>();
        m_EditorShell = CreateScope<EditorShell>();
        m_ViewportController = CreateScope<EditorViewportController>();
        m_RenderController = CreateScope<EditorRenderController>(EditorRenderController::Dependencies{
            *m_SceneRenderer,
            *m_PostProcessing,
            *m_PostProcessingSettings,
            *m_ViewportController,
            *m_PanelCoordinator,
            *m_PhysicsDebugDraw,
            m_ShowPhysicsColliders,
        });

        // 初始化
        m_ViewportController->Initialize();
        m_RenderController->Attach();

        // 面板配置
        m_HierarchyPanel->SetCommandHistory(m_CommandHistory.get());
        m_PanelCoordinator->Initialize(m_HierarchyPanel.get(), m_PropertiesPanel.get(), m_ConsolePanel.get(),
                                       m_AssetBrowserPanel.get(), m_RenderSettingsPanel.get(), m_CommandHistory.get());
        m_SelectionGizmoController->Initialize(m_PanelCoordinator.get(), m_CommandHistory.get());

        m_ConsolePanel->RegisterSink();
    }

    void EditorBootstrapper::Teardown()
    {
        if (m_ConsolePanel)
            m_ConsolePanel->UnregisterSink();
        if (m_RenderController)
            m_RenderController->Detach();
        // Scope 析构器按声明逆序自动清理，此处只需处理有序关闭
    }

    void EditorBootstrapper::SetSceneOpenCallback(std::function<void(const std::string&)> callback)
    {
        m_AssetBrowserPanel->SetSceneOpenCallback(std::move(callback));
    }

    void EditorBootstrapper::SetMSAAChangedCallback(std::function<void(uint32_t)> callback)
    {
        m_RenderSettingsPanel->SetMSAAChangedCallback(std::move(callback));
    }

} // namespace Engine
