#pragma once

#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Core/Base.h"

namespace Engine
{

    class SceneHierarchyPanel
    {
    public:
        SceneHierarchyPanel() = default;
        SceneHierarchyPanel(const Ref<Scene>& scene);

        void SetContext(const Ref<Scene>& scene);

        void OnImGuiRender();

        Entity GetSelectedEntity() const { return m_SelectionContext; }
        void SetSelectedEntity(Entity entity) { m_SelectionContext = entity; }

    private:
        void DrawEntityNode(Entity entity);

    private:
        Ref<Scene> m_Context;
        Entity m_SelectionContext;
    };

} // namespace Engine
