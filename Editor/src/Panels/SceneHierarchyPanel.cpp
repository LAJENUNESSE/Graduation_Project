#include "Panels/SceneHierarchyPanel.h"
#include "Scene/Components.h"

#include <imgui.h>

namespace Engine
{

    SceneHierarchyPanel::SceneHierarchyPanel(const Ref<Scene>& scene)
    {
        SetContext(scene);
    }

    void SceneHierarchyPanel::SetContext(const Ref<Scene>& scene)
    {
        m_Context = scene;
        m_SelectionContext = {};
    }

    void SceneHierarchyPanel::OnImGuiRender()
    {
        ImGui::Begin("场景层级");

        if (m_Context)
        {
            Entity entityToDelete;
            auto view = m_Context->GetAllEntitiesWith<TagComponent>();
            for (auto entityID : view)
            {
                Entity entity{entityID, m_Context.get()};
                DrawEntityNode(entity, entityToDelete);
            }

            // Deferred entity deletion (safe: outside iteration)
            if (entityToDelete)
            {
                if (m_SelectionContext == entityToDelete)
                    m_SelectionContext = {};
                m_Context->DestroyEntity(entityToDelete);
            }

            // Deselect when clicking on empty space (not on any item)
            if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
                m_SelectionContext = {};

            // Right-click on blank space
            if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
            {
                if (ImGui::MenuItem("创建空实体"))
                    m_Context->CreateEntity("空实体");
                ImGui::EndPopup();
            }
        }

        ImGui::End();
    }

    void SceneHierarchyPanel::DrawEntityNode(Entity entity, Entity& entityToDelete)
    {
        auto& tag = entity.GetComponent<TagComponent>().Tag;

        ImGuiTreeNodeFlags flags =
            ((m_SelectionContext == entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
        flags |= ImGuiTreeNodeFlags_SpanAvailWidth;

        bool opened =
            ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uint64_t>(static_cast<uint32_t>(entity))),
                              flags, "%s", tag.c_str());

        if (ImGui::IsItemClicked())
        {
            m_SelectionContext = entity;
        }

        // Right-click on entity
        bool entityDeleted = false;
        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("删除实体"))
                entityDeleted = true;
            ImGui::EndPopup();
        }

        if (opened)
        {
            ImGui::TreePop();
        }

        if (entityDeleted)
        {
            entityToDelete = entity;
        }
    }

} // namespace Engine
