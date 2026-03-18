#include <gtest/gtest.h>
#include "CommandHistory.h"

using namespace Engine;

// MockCommand：内部计数器 ++/-- 测试
class MockCommand : public ICommand
{
public:
    MockCommand(int& counter, const std::string& desc = "mock")
        : m_Counter(counter), m_Desc(desc)
    {
    }

    void        Execute() override { ++m_Counter; }
    void        Undo() override { --m_Counter; }
    std::string GetDescription() const override { return m_Desc; }

private:
    int&        m_Counter;
    std::string m_Desc;
};

TEST(CommandHistory, InitialState)
{
    CommandHistory history;
    EXPECT_FALSE(history.CanUndo());
    EXPECT_FALSE(history.CanRedo());
    EXPECT_EQ(history.GetUndoDescription(), "");
    EXPECT_EQ(history.GetRedoDescription(), "");
}

TEST(CommandHistory, ExecuteAndPush)
{
    CommandHistory history;
    int            counter = 0;

    history.ExecuteAndPushCommand(std::make_shared<MockCommand>(counter));
    EXPECT_EQ(counter, 1);
    EXPECT_TRUE(history.CanUndo());
    EXPECT_FALSE(history.CanRedo());
}

TEST(CommandHistory, UndoCommand)
{
    CommandHistory history;
    int            counter = 0;

    history.ExecuteAndPushCommand(std::make_shared<MockCommand>(counter));
    history.UndoCommand();
    EXPECT_EQ(counter, 0);
    EXPECT_FALSE(history.CanUndo());
    EXPECT_TRUE(history.CanRedo());
}

TEST(CommandHistory, RedoCommand)
{
    CommandHistory history;
    int            counter = 0;

    history.ExecuteAndPushCommand(std::make_shared<MockCommand>(counter));
    history.UndoCommand();
    history.RedoCommand();
    EXPECT_EQ(counter, 1);
    EXPECT_TRUE(history.CanUndo());
    EXPECT_FALSE(history.CanRedo());
}

TEST(CommandHistory, UndoRedoSequence)
{
    CommandHistory history;
    int            counter = 0;

    history.ExecuteAndPushCommand(std::make_shared<MockCommand>(counter));
    history.ExecuteAndPushCommand(std::make_shared<MockCommand>(counter));
    history.ExecuteAndPushCommand(std::make_shared<MockCommand>(counter));
    EXPECT_EQ(counter, 3);

    history.UndoCommand();
    EXPECT_EQ(counter, 2);
    history.UndoCommand();
    EXPECT_EQ(counter, 1);
    history.RedoCommand();
    EXPECT_EQ(counter, 2);
}

TEST(CommandHistory, NewCommandClearsRedoStack)
{
    CommandHistory history;
    int            counter = 0;

    history.ExecuteAndPushCommand(std::make_shared<MockCommand>(counter));
    history.ExecuteAndPushCommand(std::make_shared<MockCommand>(counter));
    history.UndoCommand(); // counter=1, redo 有 1 项

    // 新命令应清空 redo 栈
    history.ExecuteAndPushCommand(std::make_shared<MockCommand>(counter));
    EXPECT_FALSE(history.CanRedo());
    EXPECT_EQ(counter, 2);
}

TEST(CommandHistory, MaxHistoryLimit)
{
    CommandHistory history;
    int            counter = 0;

    // 超过 MaxHistory 限制
    for (size_t i = 0; i < CommandHistory::MaxHistory + 10; ++i)
        history.ExecuteAndPushCommand(std::make_shared<MockCommand>(counter));

    EXPECT_EQ(counter, static_cast<int>(CommandHistory::MaxHistory + 10));

    // Undo 最多只能回退 MaxHistory 步
    int undoCount = 0;
    while (history.CanUndo())
    {
        history.UndoCommand();
        ++undoCount;
    }
    EXPECT_EQ(undoCount, static_cast<int>(CommandHistory::MaxHistory));
}

TEST(CommandHistory, ClearHistory)
{
    CommandHistory history;
    int            counter = 0;

    history.ExecuteAndPushCommand(std::make_shared<MockCommand>(counter));
    history.ExecuteAndPushCommand(std::make_shared<MockCommand>(counter));
    history.UndoCommand();

    history.Clear();
    EXPECT_FALSE(history.CanUndo());
    EXPECT_FALSE(history.CanRedo());
}

TEST(CommandHistory, SuspendedMode)
{
    CommandHistory history;
    int            counter = 0;

    history.SetSuspended(true);
    EXPECT_TRUE(history.IsSuspended());

    history.ExecuteAndPushCommand(std::make_shared<MockCommand>(counter));
    // 命令仍然被执行
    EXPECT_EQ(counter, 1);
    // 但不入栈
    EXPECT_FALSE(history.CanUndo());

    history.SetSuspended(false);
    history.ExecuteAndPushCommand(std::make_shared<MockCommand>(counter));
    EXPECT_EQ(counter, 2);
    EXPECT_TRUE(history.CanUndo());
}

TEST(CommandHistory, PushExecutedCommand)
{
    CommandHistory history;
    int            counter = 10; // 已经执行过的值

    // PushExecutedCommand 不调用 Execute，只入栈
    history.PushExecutedCommand(std::make_shared<MockCommand>(counter));
    EXPECT_EQ(counter, 10); // 不变
    EXPECT_TRUE(history.CanUndo());

    history.UndoCommand();
    EXPECT_EQ(counter, 9); // Undo 调用 --
}

TEST(CommandHistory, GetUndoDescription)
{
    CommandHistory history;
    int            counter = 0;

    history.ExecuteAndPushCommand(std::make_shared<MockCommand>(counter, "action A"));
    EXPECT_EQ(history.GetUndoDescription(), "action A");

    history.ExecuteAndPushCommand(std::make_shared<MockCommand>(counter, "action B"));
    EXPECT_EQ(history.GetUndoDescription(), "action B");
}

TEST(CommandHistory, GetRedoDescription)
{
    CommandHistory history;
    int            counter = 0;

    history.ExecuteAndPushCommand(std::make_shared<MockCommand>(counter, "my action"));
    history.UndoCommand();
    EXPECT_EQ(history.GetRedoDescription(), "my action");
}

TEST(CommandHistory, ModifiedCallbackFired)
{
    CommandHistory history;
    int            counter = 0;
    int            callbackCount = 0;

    history.SetModifiedCallback([&callbackCount]() { callbackCount++; });

    history.ExecuteAndPushCommand(std::make_shared<MockCommand>(counter));
    EXPECT_EQ(callbackCount, 1);

    history.UndoCommand();
    EXPECT_EQ(callbackCount, 2);

    history.RedoCommand();
    EXPECT_EQ(callbackCount, 3);
}

TEST(CommandHistory, EmptyStackUndoSafe)
{
    CommandHistory history;
    // 空栈 Undo/Redo 不应崩溃
    history.UndoCommand();
    history.RedoCommand();
    EXPECT_FALSE(history.CanUndo());
    EXPECT_FALSE(history.CanRedo());
}
