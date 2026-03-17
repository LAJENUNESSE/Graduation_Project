#include "UndoSystem.h"
#include "Core/Log.h"
#include "Reflection/ComponentRegistry.h"
#include "Scene/Components.h"

namespace Engine
{
    namespace
    {
        void
        ApplyTransform(Entity entity, const glm::vec3& translation, const glm::vec3& rotation, const glm::vec3& scale)
        {
            if (!entity || !entity.HasComponent<TransformComponent>())
                return;

            auto& tc       = entity.GetComponent<TransformComponent>();
            tc.Translation = translation;
            tc.Rotation    = rotation;
            tc.Scale       = scale;
        }

        void ApplyTransformByUUID(const UUID&       entityID,
                                  const Ref<Scene>& scene,
                                  const glm::vec3&  translation,
                                  const glm::vec3&  rotation,
                                  const glm::vec3&  scale)
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

    TransformChangeCommand::TransformChangeCommand(Ref<Scene>       scene,
                                                   Entity           entity,
                                                   const glm::vec3& oldTranslation,
                                                   const glm::vec3& oldRotation,
                                                   const glm::vec3& oldScale,
                                                   const glm::vec3& newTranslation,
                                                   const glm::vec3& newRotation,
                                                   const glm::vec3& newScale)
        : m_EntityUUID(entity.GetUUID()), m_Scene(std::move(scene)), m_OldTranslation(oldTranslation),
          m_OldRotation(oldRotation), m_OldScale(oldScale), m_NewTranslation(newTranslation),
          m_NewRotation(newRotation), m_NewScale(newScale)
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
            Entity entity   = m_Scene->CreateEntity(m_Name);
            m_CreatedHandle = static_cast<entt::entity>(entity);
            m_EntityUUID    = entity.GetUUID();
        }
        else
        {
            // Redo：使用相同 UUID 重建，保持引用一致性
            Entity entity   = m_Scene->CreateEntityWithUUID(m_EntityUUID, m_Name);
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

    EntityDeleteCommand::EntityDeleteCommand(Ref<Scene> scene, Entity entity) : m_Scene(scene)
    {
        // 记录根实体的外部父节点
        if (entity.HasComponent<RelationshipComponent>())
            m_OriginalParentUUID = entity.GetComponent<RelationshipComponent>().ParentID;

        // DFS 先序收集整棵子树
        CollectSubtree(entity);
    }

    void EntityDeleteCommand::CollectSubtree(Entity entity)
    {
        EntitySnapshot snap;
        snap.EntityUUID = entity.GetUUID();
        snap.Name       = entity.GetName();
        snap.Transform  = entity.GetComponent<TransformComponent>();

        if (entity.HasComponent<RelationshipComponent>())
            snap.Relationship = entity.GetComponent<RelationshipComponent>();

        // 通过 ComponentRegistry 快照数据组件
        uint32_t eid = static_cast<uint32_t>(static_cast<entt::entity>(entity));
        for (auto& meta : ComponentRegistry::Instance().GetAll())
        {
            if (meta.Has(*m_Scene, eid) && meta.Snapshot)
                snap.Components.push_back({meta.TypeName, meta.Snapshot(*m_Scene, eid)});
        }

        m_Snapshots.push_back(std::move(snap));

        // 递归子实体
        if (entity.HasComponent<RelationshipComponent>())
        {
            auto children = entity.GetComponent<RelationshipComponent>().Children;
            for (auto childUUID : children)
            {
                Entity child = m_Scene->FindEntityByUUID(childUUID);
                if (child)
                    CollectSubtree(child);
            }
        }
    }

    void EntityDeleteCommand::Execute()
    {
        // DestroyEntity 会递归销毁子树
        Entity entity = m_Scene->FindEntityByUUID(m_Snapshots[0].EntityUUID);
        if (entity)
            m_Scene->DestroyEntity(entity);
    }

    void EntityDeleteCommand::Undo()
    {
        // 步骤 1：重建所有实体 + 恢复数据组件
        // CreateEntityWithUUID 创建的实体自带默认 RelationshipComponent（ParentID=0, Children={}）
        for (auto& snap : m_Snapshots)
        {
            Entity entity = m_Scene->CreateEntityWithUUID(snap.EntityUUID, snap.Name);

            uint32_t eid = static_cast<uint32_t>(static_cast<entt::entity>(entity));
            for (auto& compSnap : snap.Components)
            {
                auto* meta = ComponentRegistry::Instance().Find(compSnap.TypeName.c_str());
                if (meta && meta->Restore)
                    meta->Restore(*m_Scene, eid, compSnap.Data);
            }
        }

        // 步骤 2：恢复子树内部父子关系（跳过根节点 m_Snapshots[0]）
        for (size_t i = 1; i < m_Snapshots.size(); ++i)
        {
            auto& snap = m_Snapshots[i];
            if (static_cast<uint64_t>(snap.Relationship.ParentID) != 0)
            {
                Entity child  = m_Scene->FindEntityByUUID(snap.EntityUUID);
                Entity parent = m_Scene->FindEntityByUUID(snap.Relationship.ParentID);
                if (child && parent)
                    m_Scene->SetParent(child, parent);
            }
        }

        // 步骤 3：将根实体挂回子树外的原始父节点
        if (static_cast<uint64_t>(m_OriginalParentUUID) != 0)
        {
            Entity root           = m_Scene->FindEntityByUUID(m_Snapshots[0].EntityUUID);
            Entity originalParent = m_Scene->FindEntityByUUID(m_OriginalParentUUID);
            if (root && originalParent)
                m_Scene->SetParent(root, originalParent);
        }

        // 步骤 4：覆盖所有 Transform 快照（SetParent 会重算本地变换，需要恢复原值）
        for (auto& snap : m_Snapshots)
        {
            Entity entity = m_Scene->FindEntityByUUID(snap.EntityUUID);
            if (entity)
                entity.GetComponent<TransformComponent>() = snap.Transform;
        }
    }

    std::string EntityDeleteCommand::GetDescription() const
    {
        return "\xe5\x88\xa0\xe9\x99\xa4\xe5\xae\x9e\xe4\xbd\x93: " + m_Snapshots[0].Name; // 删除实体: xxx
    }

    // ==================== PropertyChangeCommand ====================

    PropertyChangeCommand::PropertyChangeCommand(const std::string& description,
                                                 std::any           oldValue,
                                                 std::any           newValue,
                                                 ApplyFn            applyFn)
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
        auto& tc         = child.GetComponent<TransformComponent>();
        m_OldTranslation = tc.Translation;
        m_OldRotation    = tc.Rotation;
        m_OldScale       = tc.Scale;
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
        auto& tc       = child.GetComponent<TransformComponent>();
        tc.Translation = m_OldTranslation;
        tc.Rotation    = m_OldRotation;
        tc.Scale       = m_OldScale;
    }

    std::string ParentChangeCommand::GetDescription() const
    {
        return "\xe4\xbf\xae\xe6\x94\xb9\xe7\x88\xb6\xe5\xad\x90\xe5\x85\xb3\xe7\xb3\xbb"; // 修改父子关系
    }

} // namespace Engine
