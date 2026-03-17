#include "UndoSystem.h"
#include "Core/Log.h"
#include "Reflection/ComponentRegistry.h"
#include "Scene/Components.h"

namespace Engine
{
    namespace
    {
        void ApplyTransform(Entity entity, const glm::vec3& translation, const glm::vec3& rotation,
                            const glm::vec3& scale)
        {
            if (!entity || !entity.HasComponent<TransformComponent>())
                return;

            auto& tc = entity.GetComponent<TransformComponent>();
            tc.Translation = translation;
            tc.Rotation = rotation;
            tc.Scale = scale;
        }

        void ApplyTransformByUUID(const UUID& entityID, const Ref<Scene>& scene, const glm::vec3& translation,
                                  const glm::vec3& rotation, const glm::vec3& scale)
        {
            if (!scene)
                return;

            ApplyTransform(scene->FindEntityByUUID(entityID), translation, rotation, scale);
        }
    } // namespace

    // ==================== CommandHistory ====================

    void CommandHistory::ExecuteAndPushCommand(Ref<ICommand> cmd)
    {
        if (!cmd)
            return;

        cmd->Execute();
        PushUndoEntry(std::move(cmd));
    }

    void CommandHistory::PushExecutedCommand(Ref<ICommand> cmd)
    {
        if (!cmd)
            return;

        PushUndoEntry(std::move(cmd));
    }

    void CommandHistory::PushUndoEntry(Ref<ICommand> cmd)
    {
        if (m_Suspended)
            return;

        m_UndoStack.push_back(std::move(cmd));

        // 清空 redo 栈（新操作后旧的 redo 分支作废）
        m_RedoStack.clear();

        // 限制历史条数
        if (m_UndoStack.size() > MaxHistory)
            m_UndoStack.erase(m_UndoStack.begin());

        if (m_ModifiedCallback)
            m_ModifiedCallback();
    }

    void CommandHistory::UndoCommand()
    {
        if (m_UndoStack.empty())
            return;

        auto cmd = m_UndoStack.back();
        m_UndoStack.pop_back();
        cmd->Undo();
        m_RedoStack.push_back(std::move(cmd));

        if (m_ModifiedCallback)
            m_ModifiedCallback();
    }

    void CommandHistory::RedoCommand()
    {
        if (m_RedoStack.empty())
            return;

        auto cmd = m_RedoStack.back();
        m_RedoStack.pop_back();
        cmd->Execute();
        m_UndoStack.push_back(std::move(cmd));

        if (m_ModifiedCallback)
            m_ModifiedCallback();
    }

    std::string CommandHistory::GetUndoDescription() const
    {
        if (m_UndoStack.empty())
            return "";
        return m_UndoStack.back()->GetDescription();
    }

    std::string CommandHistory::GetRedoDescription() const
    {
        if (m_RedoStack.empty())
            return "";
        return m_RedoStack.back()->GetDescription();
    }

    void CommandHistory::Clear()
    {
        m_UndoStack.clear();
        m_RedoStack.clear();
    }

    // ==================== TransformChangeCommand ====================

    TransformChangeCommand::TransformChangeCommand(Ref<Scene> scene, Entity entity, const glm::vec3& oldTranslation,
                                                   const glm::vec3& oldRotation, const glm::vec3& oldScale,
                                                   const glm::vec3& newTranslation, const glm::vec3& newRotation,
                                                   const glm::vec3& newScale)
        : m_EntityUUID(entity.GetUUID()), m_Scene(std::move(scene)),
          m_OldTranslation(oldTranslation), m_OldRotation(oldRotation), m_OldScale(oldScale),
          m_NewTranslation(newTranslation), m_NewRotation(newRotation), m_NewScale(newScale)
    {
    }

    void TransformChangeCommand::Execute()
    {
        ApplyTransformByUUID(m_EntityUUID, m_Scene, m_NewTranslation, m_NewRotation, m_NewScale);
    }

    void TransformChangeCommand::Undo()
    {
        ApplyTransformByUUID(m_EntityUUID, m_Scene, m_OldTranslation, m_OldRotation, m_OldScale);
    }

    std::string TransformChangeCommand::GetDescription() const
    {
        return "修改变换";
    }

    MultiTransformChangeCommand::MultiTransformChangeCommand(Ref<Scene> scene, std::vector<Entry> entries)
        : m_Scene(std::move(scene)), m_Entries(std::move(entries))
    {
    }

    void MultiTransformChangeCommand::Execute()
    {
        for (const auto& entry : m_Entries)
            ApplyTransformByUUID(entry.EntityID, m_Scene, entry.NewTranslation, entry.NewRotation, entry.NewScale);
    }

    void MultiTransformChangeCommand::Undo()
    {
        for (const auto& entry : m_Entries)
            ApplyTransformByUUID(entry.EntityID, m_Scene, entry.OldTranslation, entry.OldRotation, entry.OldScale);
    }

    std::string MultiTransformChangeCommand::GetDescription() const
    {
        return m_Entries.size() > 1 ? "修改多个实体变换" : "修改变换";
    }

    // ==================== EntityCreateCommand ====================

    EntityCreateCommand::EntityCreateCommand(Ref<Scene> scene, const std::string& name) : m_Scene(scene), m_Name(name)
    {
    }

    void EntityCreateCommand::Execute()
    {
        if (static_cast<uint64_t>(m_EntityUUID) == 0)
        {
            // 首次创建：分配新 UUID
            Entity entity = m_Scene->CreateEntity(m_Name);
            m_CreatedHandle = static_cast<entt::entity>(entity);
            m_EntityUUID = entity.GetUUID();
        }
        else
        {
            // Redo：使用相同 UUID 重建，保持引用一致性
            Entity entity = m_Scene->CreateEntityWithUUID(m_EntityUUID, m_Name);
            m_CreatedHandle = static_cast<entt::entity>(entity);
        }
    }

    void EntityCreateCommand::Undo()
    {
        if (m_CreatedHandle != entt::null)
        {
            Entity entity(m_CreatedHandle, m_Scene.get());
            if (entity)
                m_Scene->DestroyEntity(entity);
            m_CreatedHandle = entt::null;
            // 不清空 m_EntityUUID，Redo 时需要复用
        }
    }

    std::string EntityCreateCommand::GetDescription() const
    {
        return "\xe5\x88\x9b\xe5\xbb\xba\xe5\xae\x9e\xe4\xbd\x93: " + m_Name; // 创建实体: xxx
    }

    Entity EntityCreateCommand::GetCreatedEntity() const
    {
        if (m_CreatedHandle != entt::null)
            return Entity(m_CreatedHandle, m_Scene.get());
        return {};
    }

    // ==================== EntityDeleteCommand ====================

    EntityDeleteCommand::EntityDeleteCommand(Ref<Scene> scene, Entity entity)
        : m_Scene(scene), m_EntityUUID(entity.GetUUID())
    {
        // 保存快照
        m_EntityName = entity.GetName();
        m_TransformSnapshot = entity.GetComponent<TransformComponent>();

        m_HasRelationship = entity.HasComponent<RelationshipComponent>();
        if (m_HasRelationship)
            m_RelationshipSnapshot = entity.GetComponent<RelationshipComponent>();

        // 通过 ComponentRegistry 自动快照所有注册的组件
        uint32_t eid = static_cast<uint32_t>(static_cast<entt::entity>(entity));
        for (auto& meta : ComponentRegistry::Instance().GetAll())
        {
            if (meta.Has(*m_Scene, eid) && meta.Snapshot)
                m_ComponentSnapshots.push_back({meta.TypeName, meta.Snapshot(*m_Scene, eid)});
        }
    }

    void EntityDeleteCommand::Execute()
    {
        Entity entity = m_Scene->FindEntityByUUID(m_EntityUUID);
        if (entity)
            m_Scene->DestroyEntity(entity);
    }

    void EntityDeleteCommand::Undo()
    {
        // 重新创建实体并恢复组件（使用原始 UUID 以保持父子关系引用）
        Entity entity = m_Scene->CreateEntityWithUUID(m_EntityUUID, m_EntityName);

        // 通过快照恢复所有组件
        uint32_t eid = static_cast<uint32_t>(static_cast<entt::entity>(entity));
        for (auto& snap : m_ComponentSnapshots)
        {
            auto* meta = ComponentRegistry::Instance().Find(snap.TypeName.c_str());
            if (meta && meta->Restore)
                meta->Restore(*m_Scene, eid, snap.Data);
        }

        if (m_HasRelationship)
        {
            auto& relationship = entity.GetComponent<RelationshipComponent>();
            relationship.Children = m_RelationshipSnapshot.Children;

            if (static_cast<uint64_t>(m_RelationshipSnapshot.ParentID) != 0)
            {
                Entity parent = m_Scene->FindEntityByUUID(m_RelationshipSnapshot.ParentID);
                if (parent)
                    m_Scene->SetParent(entity, parent);
            }
        }

        // SetParent 会重算本地变换，这里最后覆盖回删除前快照。
        entity.GetComponent<TransformComponent>() = m_TransformSnapshot;
    }

    std::string EntityDeleteCommand::GetDescription() const
    {
        return "\xe5\x88\xa0\xe9\x99\xa4\xe5\xae\x9e\xe4\xbd\x93: " + m_EntityName; // 删除实体: xxx
    }

    // ==================== PropertyChangeCommand ====================

    PropertyChangeCommand::PropertyChangeCommand(const std::string& description, std::any oldValue, std::any newValue,
                                                 ApplyFn applyFn)
        : m_Description(description), m_OldValue(std::move(oldValue)), m_NewValue(std::move(newValue)),
          m_ApplyFn(std::move(applyFn))
    {
    }

    void PropertyChangeCommand::Execute()
    {
        if (m_ApplyFn)
            m_ApplyFn(m_NewValue);
    }

    void PropertyChangeCommand::Undo()
    {
        if (m_ApplyFn)
            m_ApplyFn(m_OldValue);
    }

    std::string PropertyChangeCommand::GetDescription() const
    {
        return m_Description;
    }

    // ==================== ParentChangeCommand ====================

    ParentChangeCommand::ParentChangeCommand(Ref<Scene> scene, Entity child, Entity newParent)
        : m_Scene(scene), m_ChildUUID(child.GetUUID()), m_NewParentUUID(newParent ? newParent.GetUUID() : UUID(0))
    {
        // 记录旧父节点 UUID
        if (child.HasComponent<RelationshipComponent>())
            m_OldParentUUID = child.GetComponent<RelationshipComponent>().ParentID;
        else
            m_OldParentUUID = 0;

        // 保存变更前子实体的本地 Transform
        auto& tc = child.GetComponent<TransformComponent>();
        m_OldTranslation = tc.Translation;
        m_OldRotation = tc.Rotation;
        m_OldScale = tc.Scale;
    }

    void ParentChangeCommand::Execute()
    {
        Entity child = m_Scene->FindEntityByUUID(m_ChildUUID);
        if (!child)
            return;

        if (static_cast<uint64_t>(m_NewParentUUID) == 0)
        {
            // 解除父子关系
            m_Scene->RemoveParent(child);
        }
        else
        {
            Entity newParent = m_Scene->FindEntityByUUID(m_NewParentUUID);
            if (newParent)
                m_Scene->SetParent(child, newParent);
        }
    }

    void ParentChangeCommand::Undo()
    {
        Entity child = m_Scene->FindEntityByUUID(m_ChildUUID);
        if (!child)
            return;

        // 先恢复到旧的父子关系
        if (static_cast<uint64_t>(m_OldParentUUID) == 0)
        {
            // 原来是根节点，解除当前父子关系
            m_Scene->RemoveParent(child);
        }
        else
        {
            Entity oldParent = m_Scene->FindEntityByUUID(m_OldParentUUID);
            if (oldParent)
                m_Scene->SetParent(child, oldParent);
        }

        // 精确还原变更前的本地 Transform（因为 SetParent/RemoveParent 做了变换转换，
        // 但浮点精度可能导致微小偏差，直接用快照值更准确）
        auto& tc = child.GetComponent<TransformComponent>();
        tc.Translation = m_OldTranslation;
        tc.Rotation = m_OldRotation;
        tc.Scale = m_OldScale;
    }

    std::string ParentChangeCommand::GetDescription() const
    {
        return "\xe4\xbf\xae\xe6\x94\xb9\xe7\x88\xb6\xe5\xad\x90\xe5\x85\xb3\xe7\xb3\xbb"; // 修改父子关系
    }

} // namespace Engine
