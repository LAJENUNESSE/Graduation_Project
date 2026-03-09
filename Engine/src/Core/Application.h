#pragma once

#include "Core/Base.h"
#include "Core/LayerStack.h"
#include "Core/Window.h"
#include "Events/Event.h"

namespace Engine
{

    class ImGuiLayer;

    class Application
    {
    public:
        Application();
        virtual ~Application();

        void Run();
        void OnEvent(Event& e);

        void PushLayer(Layer* layer);
        void PushOverlay(Layer* overlay);
        void Close() { m_Running = false; }

        ImGuiLayer* GetImGuiLayer() { return m_ImGuiLayer; }
        Window& GetWindow() { return *m_Window; }

        void SetTargetFrameRate(float fps) { m_TargetFrameRate = fps; }
        float GetTargetFrameRate() const { return m_TargetFrameRate; }
        void SetFrameRateLimitEnabled(bool enabled) { m_FrameRateLimitEnabled = enabled; }
        bool IsFrameRateLimitEnabled() const { return m_FrameRateLimitEnabled; }

        static Application& Get() { return *s_Instance; }

    private:
        bool OnWindowClose(class WindowCloseEvent& e);
        bool OnWindowResize(class WindowResizeEvent& e);

        Scope<Window> m_Window;
        ImGuiLayer* m_ImGuiLayer = nullptr;
        LayerStack m_LayerStack;
        bool m_Running = true;
        bool m_Minimized = false;
        bool m_Initialized = false;
        float m_LastFrameTime = 0.0f;
        float m_TargetFrameRate = 144.0f;
        bool m_FrameRateLimitEnabled = true;

        static Application* s_Instance;
    };

    // To be defined by the client application
    Application* CreateApplication();

} // namespace Engine