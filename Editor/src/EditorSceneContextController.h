#pragma once

#include "Core/Base.h"
#include "Scene/Scene.h"

namespace Engine
{

    class SceneHierarchyPanel;
    class AssetBrowserPanel;
    class RenderSettingsPanel;
    class CommandHistory;
    class Entity;

    class EditorSceneContextController
    {
    public:
        void Initialize(SceneHierarchyPanel* hierarchyPanel,
                        AssetBrowserPanel* assetBrowserPanel,
                        RenderSettingsPanel* renderSettingsPanel,
                        Entity* selectedEntity,
                        Entity* hoveredEntity,
                        CommandHistory* commandHistory);

        void ApplyScene(const Ref<Scene>& scene, bool clearCommandHistory);

    private:
        SceneHierarchyPanel* m_HierarchyPanel = nullptr;
        AssetBrowserPanel* m_AssetBrowserPanel = nullptr;
        RenderSettingsPanel* m_RenderSettingsPanel = nullptr;
        Entity* m_SelectedEntity = nullptr;
        Entity* m_HoveredEntity = nullptr;
        CommandHistory* m_CommandHistory = nullptr;
    };

} // namespace Engine
