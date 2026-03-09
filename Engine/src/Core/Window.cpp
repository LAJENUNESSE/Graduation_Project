#include "engpch.h"
#include "Core/Window.h"
#include "Core/Assert.h"
#include "Core/Log.h"
#include "Core/KeyCodes.h"
#include "Core/MouseCodes.h"

#include "Platform/OpenGL/OpenGLContext.h"

#include "Events/ApplicationEvent.h"
#include "Events/KeyEvent.h"
#include "Events/MouseEvent.h"

#include "Debug/PerformanceMonitor.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>

#ifdef __linux__
#include <X11/Xlib.h>
#endif

namespace Engine
{

    static uint8_t s_GLFWWindowCount = 0;

#ifdef __linux__
    static int X11ErrorHandler(Display*, XErrorEvent* event)
    {
        if (event->error_code == BadWindow)
            return 0;
        return 0;
    }
#endif

    static void GLFWErrorCallback(int error, const char* description)
    {
        ENGINE_CORE_ERROR("GLFW Error ({0}): {1}", error, description);
    }

    class GLFWWindowImpl : public Window
    {
    public:
        GLFWWindowImpl(const WindowProps& props);
        ~GLFWWindowImpl() override;

        void OnUpdate() override;

        unsigned int GetWidth() const override { return m_Data.Width; }
        unsigned int GetHeight() const override { return m_Data.Height; }

        void SetEventCallback(const EventCallbackFn& callback) override { m_Data.EventCallback = callback; }
        void SetVSync(bool enabled) override;
        bool IsVSync() const override;
        void SetCursorMode(CursorMode mode) override;
        CursorMode GetCursorMode() const override;
        bool SupportsRawMouseInput() const override;
        void SetRawMouseInput(bool enabled) override;
        bool IsRawMouseInputEnabled() const override;

        void* GetNativeWindow() const override { return m_Window; }

    private:
        void Init(const WindowProps& props);
        void Shutdown();

    private:
        GLFWwindow* m_Window = nullptr;
        Scope<OpenGLContext> m_Context;

        struct WindowData
        {
            std::string Title;
            unsigned int Width = 0;
            unsigned int Height = 0;
            bool VSync = false;
            CursorMode CurrentCursorMode = CursorMode::Normal;
            bool RawMouseInput = false;
            float PendingMouseX = 0.0f;
            float PendingMouseY = 0.0f;
            bool HasPendingMouseMove = false;
            EventCallbackFn EventCallback;
        };

        WindowData m_Data;
    };

    Scope<Window> Window::Create(const WindowProps& props)
    {
        return CreateScope<GLFWWindowImpl>(props);
    }

    GLFWWindowImpl::GLFWWindowImpl(const WindowProps& props)
    {
        Init(props);
    }

    GLFWWindowImpl::~GLFWWindowImpl()
    {
        Shutdown();
    }

    void GLFWWindowImpl::Init(const WindowProps& props)
    {
        m_Data.Title = props.Title;
        m_Data.Width = props.Width;
        m_Data.Height = props.Height;

        ENGINE_CORE_INFO("Creating window {0} ({1}, {2})", props.Title, props.Width, props.Height);

        if (s_GLFWWindowCount == 0)
        {
            int success = glfwInit();
            ENGINE_CORE_RELEASE_ASSERT(success, "Could not initialize GLFW!");
            if (!success)
                return;
            glfwSetErrorCallback(GLFWErrorCallback);

#ifdef __linux__
            XSetErrorHandler(X11ErrorHandler);
#endif
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        m_Window = glfwCreateWindow(static_cast<int>(props.Width), static_cast<int>(props.Height),
                                    m_Data.Title.c_str(), nullptr, nullptr);
        if (!m_Window)
        {
            ENGINE_CORE_ERROR("Failed to create GLFW window!");
            return;
        }
        ++s_GLFWWindowCount;

        m_Context = CreateScope<OpenGLContext>(m_Window);
        m_Context->Init();

        glfwSetWindowUserPointer(m_Window, &m_Data);
        SetVSync(false);
        SetCursorMode(CursorMode::Normal);
        SetRawMouseInput(false);

        glfwSetWindowFocusCallback(m_Window, [](GLFWwindow* window, int focused)
        {
            ImGui_ImplGlfw_WindowFocusCallback(window, focused);
        });

        glfwSetCursorEnterCallback(m_Window, [](GLFWwindow* window, int entered)
        {
            ImGui_ImplGlfw_CursorEnterCallback(window, entered);
        });

        glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            data.Width = width;
            data.Height = height;

            WindowResizeEvent event(width, height);
            data.EventCallback(event);
        });

        glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            WindowCloseEvent event;
            data.EventCallback(event);
        });

        glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods)
        {
            ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);

            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));

            switch (action)
            {
                case GLFW_PRESS:
                {
                    KeyPressedEvent event(key, 0);
                    data.EventCallback(event);
                    break;
                }
                case GLFW_RELEASE:
                {
                    KeyReleasedEvent event(key);
                    data.EventCallback(event);
                    break;
                }
                case GLFW_REPEAT:
                {
                    KeyPressedEvent event(key, 1);
                    data.EventCallback(event);
                    break;
                }
            }
        });

        glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int keycode)
        {
            ImGui_ImplGlfw_CharCallback(window, keycode);

            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            KeyTypedEvent event(static_cast<int>(keycode));
            data.EventCallback(event);
        });

        glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods)
        {
            ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);

            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));

            switch (action)
            {
                case GLFW_PRESS:
                {
                    MouseButtonPressedEvent event(button);
                    data.EventCallback(event);
                    break;
                }
                case GLFW_RELEASE:
                {
                    MouseButtonReleasedEvent event(button);
                    data.EventCallback(event);
                    break;
                }
            }
        });

        glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xOffset, double yOffset)
        {
            ImGui_ImplGlfw_ScrollCallback(window, xOffset, yOffset);

            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            MouseScrolledEvent event(static_cast<float>(xOffset), static_cast<float>(yOffset));
            data.EventCallback(event);
        });

        glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xPos, double yPos)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            data.PendingMouseX = static_cast<float>(xPos);
            data.PendingMouseY = static_cast<float>(yPos);
            data.HasPendingMouseMove = true;

            if (data.CurrentCursorMode == CursorMode::Normal)
                ImGui_ImplGlfw_CursorPosCallback(window, xPos, yPos);
        });
    }

    void GLFWWindowImpl::Shutdown()
    {
        if (m_Window)
        {
            glfwDestroyWindow(m_Window);
            m_Window = nullptr;
            --s_GLFWWindowCount;
        }

        m_Context.reset();

        if (s_GLFWWindowCount == 0)
            glfwTerminate();
    }

    void GLFWWindowImpl::OnUpdate()
    {
        if (!m_Window)
            return;

        auto& pm = PerformanceMonitor::Get();

        auto t0 = std::chrono::high_resolution_clock::now();
        glfwPollEvents();
        auto t1 = std::chrono::high_resolution_clock::now();

        if (m_Data.HasPendingMouseMove)
        {
            MouseMovedEvent event(m_Data.PendingMouseX, m_Data.PendingMouseY);
            m_Data.EventCallback(event);
            m_Data.HasPendingMouseMove = false;
        }

        m_Context->SwapBuffers();
        auto t2 = std::chrono::high_resolution_clock::now();

        pm.SetPollEventsCPU(std::chrono::duration<float, std::milli>(t1 - t0).count());
        pm.SetSwapBuffersCPU(std::chrono::duration<float, std::milli>(t2 - t1).count());
    }

    void GLFWWindowImpl::SetVSync(bool enabled)
    {
        glfwSwapInterval(enabled ? 1 : 0);
        m_Data.VSync = enabled;
    }

    bool GLFWWindowImpl::IsVSync() const
    {
        return m_Data.VSync;
    }

    void GLFWWindowImpl::SetCursorMode(CursorMode mode)
    {
        if (!m_Window || m_Data.CurrentCursorMode == mode)
            return;

        const int glfwMode = mode == CursorMode::Disabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL;
        glfwSetInputMode(m_Window, GLFW_CURSOR, glfwMode);
        m_Data.CurrentCursorMode = mode;
    }

    Window::CursorMode GLFWWindowImpl::GetCursorMode() const
    {
        return m_Data.CurrentCursorMode;
    }

    bool GLFWWindowImpl::SupportsRawMouseInput() const
    {
#if defined(GLFW_RAW_MOUSE_MOTION)
        return glfwRawMouseMotionSupported() == GLFW_TRUE;
#else
        return false;
#endif
    }

    void GLFWWindowImpl::SetRawMouseInput(bool enabled)
    {
#if defined(GLFW_RAW_MOUSE_MOTION)
        if (!m_Window || !SupportsRawMouseInput())
        {
            m_Data.RawMouseInput = false;
            return;
        }

        if (m_Data.RawMouseInput == enabled)
            return;

        glfwSetInputMode(m_Window, GLFW_RAW_MOUSE_MOTION, enabled ? GLFW_TRUE : GLFW_FALSE);
        m_Data.RawMouseInput = enabled;
#else
        (void)enabled;
        m_Data.RawMouseInput = false;
#endif
    }

    bool GLFWWindowImpl::IsRawMouseInputEnabled() const
    {
        return m_Data.RawMouseInput;
    }

} // namespace Engine
