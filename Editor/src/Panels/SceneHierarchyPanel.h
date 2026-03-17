#pragma once

#include "Core/Base.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"

#include <vector>

namespace Engine
{

    class CommandHistory;

    class SceneHierarchyPanel
    {
    public:
        SceneHierarchyPanel() = default;
        SceneHierarchyPanel(const Ref<Scene>& scene);

        void SetContext(const Ref<Scene>& scene);
        void SetCommandHistory(CommandHistory* history) { m_CommandHistory = history; }
        void SetReadOnly(bool readOnly) { m_ReadOnly = readOnly; }

        void OnImGuiRender();

        // 单选兼容（返回主选中实体）
        Entity GetSelectedEntity() const;
        void   SetSelectedEntity(Entity entity);

        // 多选接口
        const std::vector<Entity>& GetSelectedEntities() const { return m_SelectedEntities; }
        void                       ClearSelection();
        bool                       IsSelected(Entity entity) const;

    private:
        void DrawEntityNode(Entity entity, Entity& entityToDelete);
        void SelectEntity(Entity entity, bool ctrlHeld);
        void PruneInvalidSelection();
        bool IsEntityValid(Entity entity) const;

    private:
        Ref<Scene>          m_Context;
        std::vector<Entity> m_SelectedEntities;
        CommandHistory*     m_CommandHistory = nullptr;
        bool                m_ReadOnly       = false;
    };

} // namespace Engine
