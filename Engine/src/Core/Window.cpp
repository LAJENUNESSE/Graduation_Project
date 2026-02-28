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

#ifdef __linux__
#include <X11/Xlib.h>
#endif

namespace Engine
{

    static uint8_t s_GLFWWindowCount = 0;

#ifdef __linux__
    // 抑制 X11 退出时的 BadWindow 错误（VMware/Mesa 已知问题）
    static int X11ErrorHandler(Display*, XErrorEvent* event)
    {
        if (event->error_code == BadWindow)
            return 0; // 忽略
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

        void* GetNativeWindow() const override { return m_Window; }

    private:
        void Init(const WindowProps& props);
        void Shutdown();

    private:
        GLFWwindow* m_Window = nullptr;
        OpenGLContext* m_Context = nullptr;

        struct WindowData
        {
            std::string Title;
            unsigned int Width = 0;
            unsigned int Height = 0;
            bool VSync = false;
            EventCallbackFn EventCallback;
        };

        WindowData m_Data;
    };

    Window* Window::Create(const WindowProps& props)
    {
        return new GLFWWindowImpl(props);
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
            ENGINE_CORE_ASSERT(success, "Could not initialize GLFW!");
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

        m_Context = new OpenGLContext(m_Window);
        m_Context->Init();

        glfwSetWindowUserPointer(m_Window, &m_Data);
        SetVSync(false);

        // Set GLFW callbacks
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

        glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
        {
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
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            KeyTypedEvent event(static_cast<int>(keycode));
            data.EventCallback(event);
        });

        glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int /*mods*/)
        {
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
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            MouseScrolledEvent event(static_cast<float>(xOffset), static_cast<float>(yOffset));
            data.EventCallback(event);
        });

        glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xPos, double yPos)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            MouseMovedEvent event(static_cast<float>(xPos), static_cast<float>(yPos));
            data.EventCallback(event);
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

        delete m_Context;
        m_Context = nullptr;

        if (s_GLFWWindowCount == 0)
        {
            glfwTerminate();
        }
    }

    void GLFWWindowImpl::OnUpdate()
    {
        if (!m_Window)
            return;

        auto& pm = PerformanceMonitor::Get();

        auto t0 = std::chrono::high_resolution_clock::now();
        glfwPollEvents();
        auto t1 = std::chrono::high_resolution_clock::now();
        m_Context->SwapBuffers();
        auto t2 = std::chrono::high_resolution_clock::now();

        pm.SetPollEventsCPU(std::chrono::duration<float, std::milli>(t1 - t0).count());
        pm.SetSwapBuffersCPU(std::chrono::duration<float, std::milli>(t2 - t1).count());
    }

    void GLFWWindowImpl::SetVSync(bool enabled)
    {
        if (enabled)
        {
            glfwSwapInterval(1);
        }
        else
        {
            glfwSwapInterval(0);
        }

        m_Data.VSync = enabled;
    }

    bool GLFWWindowImpl::IsVSync() const
    {
        return m_Data.VSync;
    }

} // namespace Engine
