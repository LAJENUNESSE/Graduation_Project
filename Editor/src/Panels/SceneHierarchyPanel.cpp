#include "Panels/SceneHierarchyPanel.h"
#include "Core/Input.h"
#include "Core/KeyCodes.h"
#include "Scene/Components.h"
#include "UndoSystem.h"

#include <imgui.h>

#include <algorithm>

namespace Engine
{

    SceneHierarchyPanel::SceneHierarchyPanel(const Ref<Scene>& scene)
    {
        SetContext(scene);
    }

    void SceneHierarchyPanel::SetContext(const Ref<Scene>& scene)
    {
        m_Context = scene;
        m_SelectedEntities.clear();
    }

    Entity SceneHierarchyPanel::GetSelectedEntity() const
    {
        if (m_SelectedEntities.empty())
            return {};

        Entity entity = m_SelectedEntities.front();
        return IsEntityValid(entity) ? entity : Entity{};
    }

    void SceneHierarchyPanel::SetSelectedEntity(Entity entity)
    {
        m_SelectedEntities.clear();
        if (entity)
            m_SelectedEntities.push_back(entity);
    }

    void SceneHierarchyPanel::ClearSelection()
    {
        m_SelectedEntities.clear();
    }

    bool SceneHierarchyPanel::IsSelected(Entity entity) const
    {
        for (auto& e : m_SelectedEntities)
        {
            if (e == entity)
                return true;
        }
        return false;
    }

    void SceneHierarchyPanel::SelectEntity(Entity entity, bool ctrlHeld)
    {
        if (ctrlHeld)
        {
            // Ctrl+Click: 切换选中状态
            auto it = std::find_if(m_SelectedEntities.begin(), m_SelectedEntities.end(),
                                   [entity](const Entity& e) { return e == entity; });
            if (it != m_SelectedEntities.end())
                m_SelectedEntities.erase(it);
            else
                m_SelectedEntities.push_back(entity);
        }
        else
        {
            // 普通点击: 单选
            m_SelectedEntities.clear();
            if (entity)
                m_SelectedEntities.push_back(entity);
        }
    }

    void SceneHierarchyPanel::OnImGuiRender()
    {
        ImGui::Begin("\xe5\x9c\xba\xe6\x99\xaf\xe5\xb1\x82\xe7\xba\xa7");

        if (m_Context)
        {
            PruneInvalidSelection();

            Entity entityToDelete;

            // 仅遍历根实体（无父节点的实体）
            auto roots = m_Context->GetRootEntities();
            for (auto& entity : roots)
            {
                DrawEntityNode(entity, entityToDelete);
            }

            // 延迟删除实体（安全：在迭代外执行）
            if (entityToDelete)
            {
                // 从选中列表中移除被删除的实体
                m_SelectedEntities.erase(std::remove_if(m_SelectedEntities.begin(), m_SelectedEntities.end(),
                                                        [entityToDelete](const Entity& e)
                                                        { return e == entityToDelete; }),
                                         m_SelectedEntities.end());

                if (m_CommandHistory)
                {
                    auto cmd = CreateRef<EntityDeleteCommand>(m_Context, entityToDelete);
                    m_CommandHistory->ExecuteAndPushCommand(cmd);
                }
                else
                {
                    m_Context->DestroyEntity(entityToDelete);
                }
            }

            // 空白区域作为拖拽目标：拖到空白区域 = 设为根节点
            // 使用 InvisibleButton 填充窗口剩余空间来接收拖拽事件
            {
                ImVec2 availSize = ImGui::GetContentRegionAvail();
                // 确保最小高度，使空白区域可点击
                if (availSize.y < 20.0f)
                    availSize.y = 20.0f;
                ImGui::InvisibleButton("##drop_target_blank", availSize);

                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_NODE"))
                    {
                        uint64_t droppedUUID = *static_cast<const uint64_t*>(payload->Data);
                        Entity droppedEntity = m_Context->FindEntityByUUID(UUID(droppedUUID));
                        if (droppedEntity)
                        {
                            if (m_CommandHistory)
                            {
                                auto cmd = CreateRef<ParentChangeCommand>(m_Context, droppedEntity, Entity{});
                                m_CommandHistory->ExecuteAndPushCommand(cmd);
                            }
                            else
                            {
                                m_Context->RemoveParent(droppedEntity);
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // 空白区域右键菜单（挂在 InvisibleButton 上）
                if (ImGui::BeginPopupContextItem("##blank_context_menu", ImGuiPopupFlags_MouseButtonRight))
                {
                    if (ImGui::MenuItem("\xe5\x88\x9b\xe5\xbb\xba\xe7\xa9\xba\xe5\xae\x9e\xe4\xbd\x93"))
                    {
                        if (m_CommandHistory)
                        {
                            auto cmd =
                                CreateRef<EntityCreateCommand>(m_Context, "\xe7\xa9\xba\xe5\xae\x9e\xe4\xbd\x93");
                            m_CommandHistory->ExecuteAndPushCommand(cmd);
                        }
                        else
                        {
                            m_Context->CreateEntity("\xe7\xa9\xba\xe5\xae\x9e\xe4\xbd\x93");
                        }
                    }
                    ImGui::EndPopup();
                }

                // 空白区域点击取消选择
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                    m_SelectedEntities.clear();
            }

            // Delete 键批量删除
            if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete) && !m_SelectedEntities.empty())
            {
                auto toDelete = m_SelectedEntities;
                m_SelectedEntities.clear();
                for (auto& e : toDelete)
                {
                    if (e)
                    {
                        if (m_CommandHistory)
                        {
                            auto cmd = CreateRef<EntityDeleteCommand>(m_Context, e);
                            m_CommandHistory->ExecuteAndPushCommand(cmd);
                        }
                        else
                        {
                            m_Context->DestroyEntity(e);
                        }
                    }
                }
            }
        }

        ImGui::End();
    }

    void SceneHierarchyPanel::DrawEntityNode(Entity entity, Entity& entityToDelete)
    {
        auto& tag = entity.GetComponent<TagComponent>().Tag;

        // 检查是否有子节点
        bool hasChildren = false;
        if (entity.HasComponent<RelationshipComponent>())
        {
            auto& rel = entity.GetComponent<RelationshipComponent>();
            hasChildren = !rel.Children.empty();
        }

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

        // 多选高亮
        if (IsSelected(entity))
            flags |= ImGuiTreeNodeFlags_Selected;

        // 无子节点时显示为叶子节点（无展开箭头）
        if (!hasChildren)
            flags |= ImGuiTreeNodeFlags_Leaf;

        bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uint64_t>(static_cast<uint32_t>(entity))),
                                        flags, "%s", tag.c_str());

        if (ImGui::IsItemClicked())
        {
            bool ctrlHeld = ImGui::GetIO().KeyCtrl;
            SelectEntity(entity, ctrlHeld);
        }

        // 拖拽源：开始拖拽实体
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            uint64_t entityUUID = static_cast<uint64_t>(entity.GetUUID());
            ImGui::SetDragDropPayload("ENTITY_NODE", &entityUUID, sizeof(uint64_t));
            ImGui::Text("%s", tag.c_str());
            ImGui::EndDragDropSource();
        }

        // 拖拽目标：将拖入的实体设为当前实体的子节点
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_NODE"))
            {
                uint64_t droppedUUID = *static_cast<const uint64_t*>(payload->Data);
                Entity droppedEntity = m_Context->FindEntityByUUID(UUID(droppedUUID));
                if (droppedEntity && droppedEntity != entity)
                {
                    if (m_CommandHistory)
                    {
                        auto cmd = CreateRef<ParentChangeCommand>(m_Context, droppedEntity, entity);
                        m_CommandHistory->ExecuteAndPushCommand(cmd);
                    }
                    else
                    {
                        m_Context->SetParent(droppedEntity, entity);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        // 右键实体菜单
        bool entityDeleted = false;
        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("\xe5\x88\xa0\xe9\x99\xa4\xe5\xae\x9e\xe4\xbd\x93"))
                entityDeleted = true;
            if (ImGui::MenuItem("\xe8\xa7\xa3\xe9\x99\xa4\xe7\x88\xb6\xe8\x8a\x82\xe7\x82\xb9"))
            {
                if (m_CommandHistory)
                {
                    auto cmd = CreateRef<ParentChangeCommand>(m_Context, entity, Entity{});
                    m_CommandHistory->ExecuteAndPushCommand(cmd);
                }
                else
                {
                    m_Context->RemoveParent(entity);
                }
            }
            ImGui::EndPopup();
        }

        if (opened)
        {
            // 递归绘制子节点
            if (hasChildren)
            {
                auto children = m_Context->GetChildren(entity);
                for (auto& child : children)
                {
                    DrawEntityNode(child, entityToDelete);
                }
            }
            ImGui::TreePop();
        }

        if (entityDeleted)
        {
            entityToDelete = entity;
        }
    }

    void SceneHierarchyPanel::PruneInvalidSelection()
    {
        m_SelectedEntities.erase(std::remove_if(m_SelectedEntities.begin(), m_SelectedEntities.end(),
                                                [this](const Entity& entity) { return !IsEntityValid(entity); }),
                                 m_SelectedEntities.end());
    }

    bool SceneHierarchyPanel::IsEntityValid(Entity entity) const
    {
        return m_Context && entity && m_Context->GetRegistry().valid(static_cast<entt::entity>(entity));
    }
} // namespace Engine
