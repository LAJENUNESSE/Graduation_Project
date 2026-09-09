#include "engpch.h"
#include "Core/Input.h"
#include "Core/Application.h"
#include "Core/Headless.h"

#include <GLFW/glfw3.h>

namespace Engine
{

    bool Input::IsKeyPressed(KeyCode key)
    {
        // 无头进程（Server）无窗口，输入一律返回 false
        if (Headless::Enabled() || !Application::HasInstance())
            return false;

        auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        auto  state  = glfwGetKey(window, static_cast<int>(key));
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool Input::IsMouseButtonPressed(MouseCode button)
    {
        if (Headless::Enabled() || !Application::HasInstance())
            return false;

        auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        auto  state  = glfwGetMouseButton(window, static_cast<int>(button));
        return state == GLFW_PRESS;
    }

    glm::vec2 Input::GetMousePosition()
    {
        if (Headless::Enabled() || !Application::HasInstance())
            return {0.0f, 0.0f};

        auto*  window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        double xPos, yPos;
        glfwGetCursorPos(window, &xPos, &yPos);
        return {static_cast<float>(xPos), static_cast<float>(yPos)};
    }

    float Input::GetMouseX()
    {
        return GetMousePosition().x;
    }

    float Input::GetMouseY()
    {
        return GetMousePosition().y;
    }

} // namespace Engine
