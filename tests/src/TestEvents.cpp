#include <gtest/gtest.h>
#include "Events/Event.h"
#include "Events/ApplicationEvent.h"
#include "Events/KeyEvent.h"

using namespace Engine;

// --- WindowResizeEvent ---

TEST(WindowResizeEvent, Properties)
{
    WindowResizeEvent e(1920, 1080);
    EXPECT_EQ(e.GetWidth(), 1920u);
    EXPECT_EQ(e.GetHeight(), 1080u);
}

TEST(WindowResizeEvent, EventType)
{
    WindowResizeEvent e(800, 600);
    EXPECT_EQ(e.GetEventType(), EventType::WindowResize);
    EXPECT_EQ(WindowResizeEvent::GetStaticType(), EventType::WindowResize);
}

TEST(WindowResizeEvent, Category)
{
    WindowResizeEvent e(800, 600);
    EXPECT_TRUE(e.IsInCategory(EventCategoryApplication));
    EXPECT_FALSE(e.IsInCategory(EventCategoryInput));
    EXPECT_FALSE(e.IsInCategory(EventCategoryKeyboard));
}

TEST(WindowResizeEvent, ToString)
{
    WindowResizeEvent e(1920, 1080);
    std::string       str = e.ToString();
    EXPECT_NE(str.find("1920"), std::string::npos);
    EXPECT_NE(str.find("1080"), std::string::npos);
}

// --- KeyPressedEvent ---

TEST(KeyPressedEvent, Properties)
{
    KeyPressedEvent e(65, 3);
    EXPECT_EQ(e.GetKeyCode(), 65);
    EXPECT_EQ(e.GetRepeatCount(), 3);
}

TEST(KeyPressedEvent, Category)
{
    KeyPressedEvent e(32, 0);
    EXPECT_TRUE(e.IsInCategory(EventCategoryKeyboard));
    EXPECT_TRUE(e.IsInCategory(EventCategoryInput));
    EXPECT_FALSE(e.IsInCategory(EventCategoryMouse));
}

// --- EventDispatcher ---

TEST(EventDispatcher, DispatchMatchingType)
{
    WindowResizeEvent e(640, 480);
    EventDispatcher   dispatcher(e);

    bool dispatched = dispatcher.Dispatch<WindowResizeEvent>([](WindowResizeEvent& ev) {
        return true;
    });

    EXPECT_TRUE(dispatched);
    EXPECT_TRUE(e.Handled);
}

TEST(EventDispatcher, DispatchNonMatchingType)
{
    WindowResizeEvent e(640, 480);
    EventDispatcher   dispatcher(e);

    bool dispatched = dispatcher.Dispatch<WindowCloseEvent>([](WindowCloseEvent& ev) {
        return true;
    });

    EXPECT_FALSE(dispatched);
    EXPECT_FALSE(e.Handled);
}

TEST(EventDispatcher, HandledFlagPreserved)
{
    KeyPressedEvent e(65, 0);
    EventDispatcher dispatcher(e);

    dispatcher.Dispatch<KeyPressedEvent>([](KeyPressedEvent& ev) {
        return false; // 不标记为已处理
    });

    EXPECT_FALSE(e.Handled);
}

// --- IsInCategory 位运算 ---

TEST(Event, IsInCategoryBitmask)
{
    KeyPressedEvent e(65, 0);
    // KeyEvent 的 category = EventCategoryKeyboard | EventCategoryInput
    EXPECT_TRUE(e.IsInCategory(EventCategoryKeyboard));
    EXPECT_TRUE(e.IsInCategory(EventCategoryInput));
    EXPECT_FALSE(e.IsInCategory(EventCategoryApplication));
    EXPECT_FALSE(e.IsInCategory(EventCategoryMouse));
    EXPECT_FALSE(e.IsInCategory(EventCategoryMouseButton));
}
