#include "engpch.h"
#include "Core/Window.h"
#include "Core/Assert.h"
#include "Core/KeyCodes.h"
#include "Core/Log.h"
#include "Core/MouseCodes.h"

#include "Renderer/GraphicsContext.h"
#include "Renderer/RendererAPI.h"

#include "Events/ApplicationEvent.h"
#include "Events/KeyEvent.h"
#include "Events/MouseEvent.h"

#include "Debug/PerformanceMonitor.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>

#include <algorithm>
#include <cmath>

#ifdef __linux__
#include <X11/Xlib.h>
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#endif

namespace Engine
{

    static uint32_t s_GLFWWindowCount = 0;

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

    static int ComputeRectOverlapArea(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
    {
        int left   = std::max(ax, bx);
        int top    = std::max(ay, by);
        int right  = std::min(ax + aw, bx + bw);
        int bottom = std::min(ay + ah, by + bh);
        int w      = right - left;
        int h      = bottom - top;
        if (w <= 0 || h <= 0)
            return 0;
        return w * h;
    }

    // 在窗口模式下估算当前窗口所在显示器刷新率（优先最大重叠显示器）
    static float GetWindowRefreshHz(GLFWwindow* window)
    {
        if (!window)
            return 60.0f;

        if (GLFWmonitor* fullscreenMonitor = glfwGetWindowMonitor(window))
        {
            const GLFWvidmode* mode = glfwGetVideoMode(fullscreenMonitor);
            if (mode && mode->refreshRate > 0)
                return static_cast<float>(mode->refreshRate);
        }

        int           monitorCount = 0;
        GLFWmonitor** monitors     = glfwGetMonitors(&monitorCount);
        if (!monitors || monitorCount <= 0)
            return 60.0f;

        int wx = 0, wy = 0, ww = 0, wh = 0;
        glfwGetWindowPos(window, &wx, &wy);
        glfwGetWindowSize(window, &ww, &wh);

        GLFWmonitor* bestMonitor = nullptr;
        int          bestOverlap = -1;
        for (int i = 0; i < monitorCount; ++i)
        {
            int mx = 0, my = 0;
            glfwGetMonitorPos(monitors[i], &mx, &my);
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            if (!mode)
                continue;

            int overlap = ComputeRectOverlapArea(wx, wy, ww, wh, mx, my, mode->width, mode->height);
            if (overlap > bestOverlap)
            {
                bestOverlap = overlap;
                bestMonitor = monitors[i];
            }
        }

        if (!bestMonitor)
            bestMonitor = glfwGetPrimaryMonitor();

        const GLFWvidmode* bestMode = bestMonitor ? glfwGetVideoMode(bestMonitor) : nullptr;
        if (!bestMode || bestMode->refreshRate <= 0)
            return 60.0f;
        return static_cast<float>(bestMode->refreshRate);
    }

    static uint32_t EstimateMissedVBlank(float swapMs, float refreshPeriodMs, bool vsyncEnabled)
    {
        if (swapMs <= 0.0f || refreshPeriodMs <= 0.0f)
            return 0;

        const int intervals = static_cast<int>(std::lround(swapMs / refreshPeriodMs));
        const int expected  = vsyncEnabled ? 1 : 0;
        if (intervals <= expected)
            return 0;
        return static_cast<uint32_t>(intervals - expected);
    }

    static float GetSwapBurstThresholdMs(float refreshPeriodMs)
    {
        if (refreshPeriodMs <= 0.0f)
            return 16.0f;
        // 以“至少 2 个刷新周期”为异常阈值，且不低于 8ms
        return std::max(8.0f, refreshPeriodMs * 2.0f);
    }

    class GLFWWindowImpl : public Window
    {
    public:
        GLFWWindowImpl(const WindowProps& props);
        ~GLFWWindowImpl() override;

        void OnUpdate() override;

        unsigned int GetWidth() const override { return m_Data.Width; }
        unsigned int GetHeight() const override { return m_Data.Height; }

        void       SetEventCallback(const EventCallbackFn& callback) override { m_Data.EventCallback = callback; }
        void       SetTitle(const std::string& title) override;
        void       SetVSync(bool enabled) override;
        bool       IsVSync() const override;
        void       SetCursorMode(CursorMode mode) override;
        CursorMode GetCursorMode() const override;
        bool       SupportsRawMouseInput() const override;
        void       SetRawMouseInput(bool enabled) override;
        bool       IsRawMouseInputEnabled() const override;

        void* GetNativeWindow() const override { return m_Window; }

        GraphicsContext* GetGraphicsContext() const override { return m_Context.get(); }

    private:
        void Init(const WindowProps& props);
        void Shutdown();

    private:
        GLFWwindow*            m_Window = nullptr;
        Scope<GraphicsContext> m_Context;

        // SwapBuffers burst diagnostics（常开轻量）
        bool     m_SwapBurstActive    = false;
        uint32_t m_SwapBurstSeq       = 0;
        uint32_t m_SwapBurstLen       = 0;
        float    m_SwapBurstMaxMs     = 0.0f;
        uint32_t m_SwapBurstMissedMax = 0;

        uint32_t m_LastSwapBurstId        = 0;
        uint32_t m_LastSwapBurstLen       = 0;
        float    m_LastSwapBurstMaxMs     = 0.0f;
        uint32_t m_LastSwapBurstMissedMax = 0;

        struct WindowData
        {
            std::string     Title;
            unsigned int    Width               = 0;
            unsigned int    Height              = 0;
            bool            VSync               = false;
            CursorMode      CurrentCursorMode   = CursorMode::Normal;
            bool            RawMouseInput       = false;
            float           PendingMouseX       = 0.0f;
            float           PendingMouseY       = 0.0f;
            bool            HasPendingMouseMove = false;
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
        m_Data.Title  = props.Title;
        m_Data.Width  = props.Width;
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

        // Set GLFW window hints based on the selected rendering API
        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL)
        {
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
        }
        else if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
        {
            // Vulkan: Tell GLFW not to create an OpenGL context
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        }

        m_Window = glfwCreateWindow(static_cast<int>(props.Width), static_cast<int>(props.Height), m_Data.Title.c_str(),
                                    nullptr, nullptr);
        if (!m_Window)
        {
#ifdef __APPLE__
            ENGINE_CORE_ERROR("Failed to create GLFW window!");
            ENGINE_CORE_ERROR("This engine requires OpenGL 4.3, but macOS only supports up to 4.1.");
            ENGINE_CORE_ERROR("Consider using CrossOver or Parallels Desktop to run the Windows build.");
#else
            ENGINE_CORE_ERROR("Failed to create GLFW window!");
#endif
            return;
        }
        ++s_GLFWWindowCount;

        m_Context = GraphicsContext::Create(m_Window);
        m_Context->Init();

#ifdef _WIN32
        {
            HWND hwnd     = glfwGetWin32Window(m_Window);
            BOOL darkMode = TRUE;
            // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 11+), fall back to 19 (Windows 10)
            if (FAILED(DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(darkMode))))
                DwmSetWindowAttribute(hwnd, 19, &darkMode, sizeof(darkMode));
        }
#endif

        glfwSetWindowUserPointer(m_Window, &m_Data);
        SetVSync(false);
        SetCursorMode(CursorMode::Normal);
        SetRawMouseInput(false);

        glfwSetWindowFocusCallback(m_Window, [](GLFWwindow* window, int focused)
                                   { ImGui_ImplGlfw_WindowFocusCallback(window, focused); });

        glfwSetCursorEnterCallback(m_Window, [](GLFWwindow* window, int entered)
                                   { ImGui_ImplGlfw_CursorEnterCallback(window, entered); });

        glfwSetWindowSizeCallback(m_Window,
                                  [](GLFWwindow* window, int width, int height)
                                  {
                                      WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
                                      data.Width       = width;
                                      data.Height      = height;

                                      WindowResizeEvent event(width, height);
                                      data.EventCallback(event);
                                  });

        glfwSetWindowCloseCallback(m_Window,
                                   [](GLFWwindow* window)
                                   {
                                       WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
                                       WindowCloseEvent event;
                                       data.EventCallback(event);
                                   });

        glfwSetKeyCallback(m_Window,
                           [](GLFWwindow* window, int key, int scancode, int action, int mods)
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

        glfwSetCharCallback(m_Window,
                            [](GLFWwindow* window, unsigned int keycode)
                            {
                                ImGui_ImplGlfw_CharCallback(window, keycode);

                                WindowData&   data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
                                KeyTypedEvent event(static_cast<int>(keycode));
                                data.EventCallback(event);
                            });

        glfwSetMouseButtonCallback(m_Window,
                                   [](GLFWwindow* window, int button, int action, int mods)
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

        glfwSetScrollCallback(m_Window,
                              [](GLFWwindow* window, double xOffset, double yOffset)
                              {
                                  ImGui_ImplGlfw_ScrollCallback(window, xOffset, yOffset);

                                  WindowData&        data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
                                  MouseScrolledEvent event(static_cast<float>(xOffset), static_cast<float>(yOffset));
                                  data.EventCallback(event);
                              });

        glfwSetCursorPosCallback(m_Window,
                                 [](GLFWwindow* window, double xPos, double yPos)
                                 {
                                     WindowData& data   = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
                                     data.PendingMouseX = static_cast<float>(xPos);
                                     data.PendingMouseY = static_cast<float>(yPos);
                                     data.HasPendingMouseMove = true;
                                     // ImGui 回调移到 OnUpdate 中合并调用
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
            // 合并本帧所有鼠标移动，只向 ImGui 发送最终位置
            if (m_Data.CurrentCursorMode == CursorMode::Normal)
                ImGui_ImplGlfw_CursorPosCallback(m_Window, m_Data.PendingMouseX, m_Data.PendingMouseY);
            MouseMovedEvent event(m_Data.PendingMouseX, m_Data.PendingMouseY);
            m_Data.EventCallback(event);
            m_Data.HasPendingMouseMove = false;
        }

        m_Context->SwapBuffers();
        auto t2 = std::chrono::high_resolution_clock::now();

        const float pollMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
        const float swapMs = std::chrono::duration<float, std::milli>(t2 - t1).count();
        pm.SetPollEventsCPU(pollMs);
        pm.SetSwapBuffersCPU(swapMs);

        const float    refreshHz       = GetWindowRefreshHz(m_Window);
        const float    refreshPeriodMs = (refreshHz > 0.0f) ? (1000.0f / refreshHz) : 0.0f;
        const uint32_t missedVBlank    = EstimateMissedVBlank(swapMs, refreshPeriodMs, m_Data.VSync);
        const float    burstThreshold  = GetSwapBurstThresholdMs(refreshPeriodMs);
        const bool     swapAnomaly     = swapMs >= burstThreshold;

        if (swapAnomaly)
        {
            if (!m_SwapBurstActive)
            {
                m_SwapBurstActive = true;
                m_SwapBurstSeq++;
                m_SwapBurstLen       = 0;
                m_SwapBurstMaxMs     = 0.0f;
                m_SwapBurstMissedMax = 0;
                ENGINE_CORE_WARN(
                    "[Perf][SwapBurst] START id={0} swap={1:.3f}ms threshold={2:.3f}ms refresh={3:.1f}Hz vsync={4}",
                    m_SwapBurstSeq, swapMs, burstThreshold, refreshHz, m_Data.VSync ? 1 : 0);
            }

            m_SwapBurstLen++;
            m_SwapBurstMaxMs     = std::max(m_SwapBurstMaxMs, swapMs);
            m_SwapBurstMissedMax = std::max(m_SwapBurstMissedMax, missedVBlank);
        }
        else if (m_SwapBurstActive)
        {
            m_LastSwapBurstId        = m_SwapBurstSeq;
            m_LastSwapBurstLen       = m_SwapBurstLen;
            m_LastSwapBurstMaxMs     = m_SwapBurstMaxMs;
            m_LastSwapBurstMissedMax = m_SwapBurstMissedMax;

            ENGINE_CORE_WARN(
                "[Perf][SwapBurst] END id={0} len={1} maxSwap={2:.3f}ms maxMissedVBlank={3} refresh={4:.1f}Hz",
                m_LastSwapBurstId, m_LastSwapBurstLen, m_LastSwapBurstMaxMs, m_LastSwapBurstMissedMax, refreshHz);

            m_SwapBurstActive    = false;
            m_SwapBurstLen       = 0;
            m_SwapBurstMaxMs     = 0.0f;
            m_SwapBurstMissedMax = 0;
        }

        const uint32_t displayBurstId        = m_SwapBurstActive ? m_SwapBurstSeq : m_LastSwapBurstId;
        const uint32_t displayBurstLen       = m_SwapBurstActive ? m_SwapBurstLen : m_LastSwapBurstLen;
        const float    displayBurstMaxMs     = m_SwapBurstActive ? m_SwapBurstMaxMs : m_LastSwapBurstMaxMs;
        const uint32_t displayBurstMissedMax = m_SwapBurstActive ? m_SwapBurstMissedMax : m_LastSwapBurstMissedMax;
        pm.SetPresentDiagnostics(refreshHz, refreshPeriodMs, missedVBlank, displayBurstId, displayBurstLen,
                                 displayBurstMaxMs, displayBurstMissedMax, m_SwapBurstActive);
    }

    void GLFWWindowImpl::SetTitle(const std::string& title)
    {
        m_Data.Title = title;
        if (m_Window)
            glfwSetWindowTitle(m_Window, title.c_str());
    }

    void GLFWWindowImpl::SetVSync(bool enabled)
    {
        // glfwSwapInterval only applies to OpenGL contexts
        // For Vulkan, VSync is controlled via Swapchain PresentMode
        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL)
        {
            glfwSwapInterval(enabled ? 1 : 0);
        }
        // TODO: For Vulkan, implement VSync toggle via Swapchain recreation
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
