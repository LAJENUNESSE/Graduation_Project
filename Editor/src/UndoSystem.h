#pragma once

#include "Core/Base.h"
#include "Core/UUID.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"

#include <any>
#include <functional>
#include <stack>
#include <string>
#include <vector>

namespace Engine
{

    // ========== 命令接口 ==========
    class ICommand
    {
    public:
        virtual ~ICommand() = default;
        virtual void Execute() = 0;
        virtual void Undo() = 0;
        virtual std::string GetDescription() const = 0;
    };

    // ========== 命令历史管理 ==========
    class CommandHistory
    {
    public:
        static constexpr size_t MaxHistory = 100;

        void ExecuteAndPushCommand(Ref<ICommand> cmd);
        void PushExecutedCommand(Ref<ICommand> cmd);

        void UndoCommand();
        void RedoCommand();

        bool CanUndo() const { return !m_UndoStack.empty(); }
        bool CanRedo() const { return !m_RedoStack.empty(); }

        // 获取栈顶描述（用于菜单显示）
        std::string GetUndoDescription() const;
        std::string GetRedoDescription() const;

        void Clear();

        // Play 模式暂停命令记录
        void SetSuspended(bool suspended) { m_Suspended = suspended; }
        bool IsSuspended() const { return m_Suspended; }

    private:
        void PushUndoEntry(Ref<ICommand> cmd);

        std::vector<Ref<ICommand>> m_UndoStack;
        std::vector<Ref<ICommand>> m_RedoStack;
        bool m_Suspended = false;
    };

    // ========== Transform 修改命令 ==========
    class TransformChangeCommand : public ICommand
    {
    public:
        TransformChangeCommand(Ref<Scene> scene, Entity entity, const glm::vec3& oldTranslation,
                               const glm::vec3& oldRotation, const glm::vec3& oldScale,
                               const glm::vec3& newTranslation, const glm::vec3& newRotation,
                               const glm::vec3& newScale);

        void Execute() override;
        void Undo() override;
        std::string GetDescription() const override;

    private:
        UUID m_EntityUUID;
        Ref<Scene> m_Scene;

        glm::vec3 m_OldTranslation, m_OldRotation, m_OldScale;
        glm::vec3 m_NewTranslation, m_NewRotation, m_NewScale;
    };

    class MultiTransformChangeCommand : public ICommand
    {
    public:
        struct Entry
        {
            UUID EntityID = 0;
            glm::vec3 OldTranslation = {};
            glm::vec3 OldRotation = {};
            glm::vec3 OldScale = {1.0f, 1.0f, 1.0f};
            glm::vec3 NewTranslation = {};
            glm::vec3 NewRotation = {};
            glm::vec3 NewScale = {1.0f, 1.0f, 1.0f};
        };

        MultiTransformChangeCommand(Ref<Scene> scene, std::vector<Entry> entries);

        void Execute() override;
        void Undo() override;
        std::string GetDescription() const override;

    private:
        Ref<Scene> m_Scene;
        std::vector<Entry> m_Entries;
    };
    // ========== 实体创建命令 ==========
    class EntityCreateCommand : public ICommand
    {
    public:
        EntityCreateCommand(Ref<Scene> scene, const std::string& name);

        void Execute() override;
        void Undo() override;
        std::string GetDescription() const override;

        Entity GetCreatedEntity() const;

    private:
        Ref<Scene> m_Scene;
        std::string m_Name;
        entt::entity m_CreatedHandle = entt::null;
    };

    // ========== 实体删除命令 ==========
    class EntityDeleteCommand : public ICommand
    {
    public:
        EntityDeleteCommand(Ref<Scene> scene, Entity entity);

        void Execute() override;
        void Undo() override;
        std::string GetDescription() const override;

    private:
        struct ComponentSnapshot
        {
            std::string TypeName;
            std::any Data;
        };

        Ref<Scene> m_Scene;
        UUID m_EntityUUID;
        std::string m_EntityName;

        // 核心组件快照（始终存在）
        TransformComponent m_TransformSnapshot;
        bool m_HasRelationship = false;
        RelationshipComponent m_RelationshipSnapshot;

        // 数据驱动组件快照（通过 ComponentRegistry 自动收集）
        std::vector<ComponentSnapshot> m_ComponentSnapshots;
    };

    // ========== 泛型属性修改命令 ==========
    // 通过 lambda 和 std::any 实现类型擦除
    class PropertyChangeCommand : public ICommand
    {
    public:
        using ApplyFn = std::function<void(const std::any&)>;

        PropertyChangeCommand(const std::string& description, std::any oldValue, std::any newValue, ApplyFn applyFn);

        void Execute() override;
        void Undo() override;
        std::string GetDescription() const override;

    private:
        std::string m_Description;
        std::any m_OldValue;
        std::any m_NewValue;
        ApplyFn m_ApplyFn;
    };

    // ========== 父子关系变更命令 ==========
    // 支持 SetParent 和 RemoveParent 操作的撤销/重做
    class ParentChangeCommand : public ICommand
    {
    public:
        // newParent 为无效 Entity 时表示解除父子关系（RemoveParent）
        ParentChangeCommand(Ref<Scene> scene, Entity child, Entity newParent);

        void Execute() override;
        void Undo() override;
        std::string GetDescription() const override;

    private:
        Ref<Scene> m_Scene;
        UUID m_ChildUUID;
        UUID m_OldParentUUID; // 0 = 原来是根节点
        UUID m_NewParentUUID; // 0 = 解除父子关系
        // 保存变更前子实体的本地 Transform，用于精确还原
        glm::vec3 m_OldTranslation, m_OldRotation, m_OldScale;
    };

} // namespace Engine
