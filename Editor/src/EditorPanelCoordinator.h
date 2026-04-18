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
                        CommandHistory*      commandHistory);

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

    private:
        void RenderStatsPanel();

    private:
        SceneHierarchyPanel* m_HierarchyPanel      = nullptr;
        PropertiesPanel*     m_PropertiesPanel     = nullptr;
        ConsolePanel*        m_ConsolePanel        = nullptr;
        AssetBrowserPanel*   m_AssetBrowserPanel   = nullptr;
        RenderSettingsPanel* m_RenderSettingsPanel = nullptr;
        CommandHistory*      m_CommandHistory      = nullptr;
        SceneRenderer*       m_SceneRenderer       = nullptr;
        bool                 m_ShowStatsPanel      = true;
    };

} // namespace Engine
