#pragma once

#include "Core/Base.h"
#include "Events/Event.h"

#include <functional>
#include <string>

namespace Engine
{

    class GraphicsContext;

    struct WindowProps
    {
        std::string  Title;
        unsigned int Width;
        unsigned int Height;

        WindowProps(const std::string& title = "Game Engine", unsigned int width = 1280, unsigned int height = 720)
            : Title(title), Width(width), Height(height)
        {
        }
    };

    class Window
    {
    public:
        using EventCallbackFn = std::function<void(Event&)>;

        enum class CursorMode
        {
            Normal = 0,
            Disabled
        };

        virtual ~Window() = default;

        virtual void OnUpdate() = 0;

        virtual unsigned int GetWidth() const  = 0;
        virtual unsigned int GetHeight() const = 0;

        virtual void       SetEventCallback(const EventCallbackFn& callback) = 0;
        virtual void       SetVSync(bool enabled)                            = 0;
        virtual bool       IsVSync() const                                   = 0;
        virtual void       SetTitle(const std::string& title)                = 0;
        virtual void       SetCursorMode(CursorMode mode)                    = 0;
        virtual CursorMode GetCursorMode() const                             = 0;
        virtual bool       SupportsRawMouseInput() const                     = 0;
        virtual void       SetRawMouseInput(bool enabled)                    = 0;
        virtual bool       IsRawMouseInputEnabled() const                    = 0;

        virtual void* GetNativeWindow() const = 0;

        // 暴露 GraphicsContext 供 Application::Run 显式驱动帧边界
        // (BeginRenderFrame / EndRenderFrame) — Vulkan 路径需要在 OnUpdate 之前
        // 开始帧，使粒子/流体 dispatch 能录入主帧 cmd。
        virtual GraphicsContext* GetGraphicsContext() const = 0;

        static Scope<Window> Create(const WindowProps& props = WindowProps());
    };

} // namespace Engine
