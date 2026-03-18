#include "CommandHistory.h"

namespace Engine
{

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

} // namespace Engine
