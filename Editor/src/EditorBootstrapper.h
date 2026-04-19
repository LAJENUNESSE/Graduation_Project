#pragma once

#include "Core/Base.h"

#include <functional>
#include <string>

namespace Engine
{
    // 前置声明所有子系统
    class SceneRenderer;
    class PostProcessing;
    struct PostProcessingSettings;
    class PhysicsDebugDraw;
    class EditorSceneSession;
    class EditorPanelCoordinator;
    class EditorSelectionGizmoController;
    class EditorShell;
    class EditorViewportController;
    class EditorRenderController;
    class SceneHierarchyPanel;
    class PropertiesPanel;
    class ConsolePanel;
    class AssetBrowserPanel;
    class RenderSettingsPanel;
    class ScriptEditorPanel;
    class CommandHistory;

    class EditorBootstrapper
    {
    public:
        EditorBootstrapper();
        ~EditorBootstrapper();

        void Assemble(); // 创建所有对象 + 初始化 + 连线
        void Teardown(); // 逆序清理

        // 回调设置（捕获 EditorLayer::this 的回调必须由 EditorLayer 在 Assemble 后设置）
        void SetSceneOpenCallback(std::function<void(const std::string&)> callback);
        void SetMSAAChangedCallback(std::function<void(uint32_t)> callback);
        void SetModifiedCallback(std::function<void()> callback);

        // 访问器（返回引用，不转移所有权）
        EditorSceneSession&             SceneSession() { return *m_SceneSession; }
        EditorPanelCoordinator&         PanelCoordinator() { return *m_PanelCoordinator; }
        EditorSelectionGizmoController& SelectionGizmoController() { return *m_SelectionGizmoController; }
        EditorShell&                    Shell() { return *m_EditorShell; }
        EditorViewportController&       ViewportController() { return *m_ViewportController; }
        EditorRenderController&         RenderController() { return *m_RenderController; }
        RenderSettingsPanel&            GetRenderSettingsPanel() { return *m_RenderSettingsPanel; }
        CommandHistory&                 GetCommandHistory() { return *m_CommandHistory; }
        bool&                           ShowPhysicsColliders() { return m_ShowPhysicsColliders; }
        bool&                           ShowMeshSDFBounds() { return m_ShowMeshSDFBounds; }

    private:
        // 基础设施（先创建）
        Scope<SceneRenderer>          m_SceneRenderer;
        Scope<PostProcessing>         m_PostProcessing;
        Scope<PostProcessingSettings> m_PostProcessingSettings;
        Scope<PhysicsDebugDraw>       m_PhysicsDebugDraw;
        Scope<CommandHistory>         m_CommandHistory;
        bool                          m_ShowPhysicsColliders = false;
        bool                          m_ShowMeshSDFBounds    = false;

        // 面板
        Scope<SceneHierarchyPanel> m_HierarchyPanel;
        Scope<PropertiesPanel>     m_PropertiesPanel;
        Scope<ConsolePanel>        m_ConsolePanel;
        Scope<AssetBrowserPanel>   m_AssetBrowserPanel;
        Scope<RenderSettingsPanel> m_RenderSettingsPanel;
        Scope<ScriptEditorPanel>   m_ScriptEditorPanel;

        // 控制器（依赖基础设施）
        Scope<EditorSceneSession>             m_SceneSession;
        Scope<EditorPanelCoordinator>         m_PanelCoordinator;
        Scope<EditorSelectionGizmoController> m_SelectionGizmoController;
        Scope<EditorShell>                    m_EditorShell;
        Scope<EditorViewportController>       m_ViewportController;
        Scope<EditorRenderController>         m_RenderController;
    };

} // namespace Engine
