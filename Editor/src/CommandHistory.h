#pragma once

// ICommand + CommandHistory —— 从 UndoSystem.h 拆出的纯逻辑部分。
// 仅依赖 Core/Base.h + 标准库，可在单元测试中直接使用。

#include "Core/Base.h"

#include <functional>
#include <string>
#include <vector>

namespace Engine
{

    // ========== 命令接口 ==========
    class ICommand
    {
    public:
        virtual ~ICommand()                        = default;
        virtual void        Execute()              = 0;
        virtual void        Undo()                 = 0;
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

        // 场景修改通知回调（用于桥接脏标记）
        using ModifiedCallback = std::function<void()>;
        void SetModifiedCallback(ModifiedCallback cb) { m_ModifiedCallback = std::move(cb); }

    private:
        void PushUndoEntry(Ref<ICommand> cmd);

        std::vector<Ref<ICommand>> m_UndoStack;
        std::vector<Ref<ICommand>> m_RedoStack;
        bool                       m_Suspended = false;
        ModifiedCallback           m_ModifiedCallback;
    };

} // namespace Engine
