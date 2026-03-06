#include "EditorSceneContextController.h"

#include "Panels/AssetBrowserPanel.h"
#include "Panels/RenderSettingsPanel.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Scene/Entity.h"
#include "UndoSystem.h"

namespace Engine
{

    void EditorSceneContextController::Initialize(SceneHierarchyPanel* hierarchyPanel,
                                                  AssetBrowserPanel* assetBrowserPanel,
                                                  RenderSettingsPanel* renderSettingsPanel,
                                                  Entity* selectedEntity,
                                                  Entity* hoveredEntity,
                                                  CommandHistory* commandHistory)
    {
        m_HierarchyPanel = hierarchyPanel;
        m_AssetBrowserPanel = assetBrowserPanel;
        m_RenderSettingsPanel = renderSettingsPanel;
        m_SelectedEntity = selectedEntity;
        m_HoveredEntity = hoveredEntity;
        m_CommandHistory = commandHistory;
    }

    void EditorSceneContextController::ApplyScene(const Ref<Scene>& scene, bool clearCommandHistory)
    {
        if (m_HierarchyPanel)
            m_HierarchyPanel->SetContext(scene);
        if (m_AssetBrowserPanel)
            m_AssetBrowserPanel->SetContext(scene);
        if (m_RenderSettingsPanel)
            m_RenderSettingsPanel->SetScene(scene);

        if (m_SelectedEntity)
            *m_SelectedEntity = {};
        if (m_HoveredEntity)
            *m_HoveredEntity = {};

        if (clearCommandHistory && m_CommandHistory)
            m_CommandHistory->Clear();
    }

} // namespace Engine