#pragma once

#include "Core/Base.h"
#include "Scene/Scene.h"

#include <functional>
#include <vector>

namespace Engine
{

    class SceneHierarchyPanel;
    class PropertiesPanel;
    class ConsolePanel;
    class AssetBrowserPanel;
    class RenderSettingsPanel;
    class ScriptEditorPanel;
    class MemoryStatsPanel;
    class CommandHistory;
    class Entity;
    class Framebuffer;
    class SceneRenderer;

    class EditorPanelCoordinator
    {
    public:
        void Initialize(SceneHierarchyPanel* hierarchyPanel,
                        PropertiesPanel*     propertiesPanel,
                        ConsolePanel*        consolePanel,
                        AssetBrowserPanel*   assetBrowserPanel,
                        RenderSettingsPanel* renderSettingsPanel,
                        ScriptEditorPanel*   scriptEditorPanel,
                        CommandHistory*      commandHistory);

        // 构造/析构定义在 cpp：成员含不完整类型的 unique_ptr
        EditorPanelCoordinator();
        ~EditorPanelCoordinator();

        void ApplyScene(const Ref<Scene>& scene, bool clearCommandHistory);
        void RenderPanels();
        void SetHDRFramebuffer(const Ref<Framebuffer>& framebuffer);
        void SetSceneRenderer(SceneRenderer* sceneRenderer) { m_SceneRenderer = sceneRenderer; }
        void SetPanelsReadOnly(bool readOnly);
        void SetSceneModifiedCallback(std::function<void()> cb);

        Entity                     GetPrimarySelection() const;
        const std::vector<Entity>& GetSelectedEntities() const;
        void                       SetPrimarySelection(Entity entity);
        void                       ClearSelection();

        bool IsStatsPanelVisible() const { return m_ShowStatsPanel; }
        void SetStatsPanelVisible(bool visible) { m_ShowStatsPanel = visible; }
        void ToggleStatsPanelVisible() { m_ShowStatsPanel = !m_ShowStatsPanel; }

        // 显存与内存监控面板（视图菜单切换）
        bool IsMemoryPanelVisible() const { return m_ShowMemoryPanel; }
        void SetMemoryPanelVisible(bool visible) { m_ShowMemoryPanel = visible; }
        void ToggleMemoryPanelVisible() { m_ShowMemoryPanel = !m_ShowMemoryPanel; }

        // 脚本编辑面板可见性（视图菜单切换）
        bool IsScriptEditorVisible() const;
        void SetScriptEditorVisible(bool visible);
        void ToggleScriptEditorVisible();
        void OpenScript(const std::string& path);

    private:
        void RenderStatsPanel();

    private:
        SceneHierarchyPanel*    m_HierarchyPanel      = nullptr;
        PropertiesPanel*        m_PropertiesPanel     = nullptr;
        ConsolePanel*           m_ConsolePanel        = nullptr;
        AssetBrowserPanel*      m_AssetBrowserPanel   = nullptr;
        RenderSettingsPanel*    m_RenderSettingsPanel = nullptr;
        ScriptEditorPanel*      m_ScriptEditorPanel   = nullptr;
        CommandHistory*         m_CommandHistory      = nullptr;
        SceneRenderer*          m_SceneRenderer       = nullptr;
        Scope<MemoryStatsPanel> m_MemoryStatsPanel;
        bool                    m_ShowStatsPanel  = true;
        bool                    m_ShowMemoryPanel = false;
    };

} // namespace Engine
