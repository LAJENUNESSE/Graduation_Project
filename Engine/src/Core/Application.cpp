#include "engpch.h"
#include "Core/Application.h"
#include "Core/Log.h"
#include "Core/Assert.h"
#include "Core/Timestep.h"
#include "Events/ApplicationEvent.h"
#include "ImGui/ImGuiLayer.h"
#include "Renderer/Renderer.h"
#include "Asset/AssetManager.h"
#include "Debug/PerformanceMonitor.h"
#include "Debug/ProfileTimer.h"

#include <GLFW/glfw3.h>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace Engine
{

    Application* Application::s_Instance = nullptr;

    Application::Application()
    {
        ENGINE_CORE_ASSERT(!s_Instance, "Application already exists!");
        s_Instance = this;

        WindowProps props;
        m_Window = Window::Create(props);

        if (!m_Window->GetNativeWindow())
        {
            ENGINE_CORE_ERROR("Window creation failed! Application cannot continue.");
            m_Running = false;
            return;
        }

        m_Window->SetEventCallback([this](Event& e) { OnEvent(e); });

        Renderer::Init();
        AssetManager::Init();
        PerformanceMonitor::Get().Init();

        auto imguiLayer = CreateScope<ImGuiLayer>();
        m_ImGuiLayer = imguiLayer.get();
        PushOverlay(std::move(imguiLayer));

        m_Initialized = true;

#ifdef _WIN32
        timeBeginPeriod(1);
#endif
    }

    Application::~Application()
    {
#ifdef _WIN32
        timeEndPeriod(1);
#endif
        if (m_Initialized)
        {
            PerformanceMonitor::Get().Shutdown();
            AssetManager::Shutdown();
        }
        Renderer::Shutdown();
        s_Instance = nullptr;
    }

    void Application::PushLayer(Scope<Layer> layer)
    {
        Layer* layerPtr = layer.get();
        m_LayerStack.PushLayer(std::move(layer));
        layerPtr->OnAttach();
    }

    void Application::PushOverlay(Scope<Layer> overlay)
    {
        Layer* overlayPtr = overlay.get();
        m_LayerStack.PushOverlay(std::move(overlay));
        overlayPtr->OnAttach();
    }

    void Application::OnEvent(Event& e)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& e) { return OnWindowClose(e); });
        dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& e) { return OnWindowResize(e); });

        for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
        {
            if (e.Handled)
                break;
            (*it)->OnEvent(e);
        }
    }

    bool Application::OnWindowClose(WindowCloseEvent& e)
    {
        m_Running = false;
        return true;
    }

    bool Application::OnWindowResize(WindowResizeEvent& e)
    {
        if (e.GetWidth() == 0 || e.GetHeight() == 0)
        {
            m_Minimized = true;
            return false;
        }

        m_Minimized = false;
        Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());
        return false;
    }

    void Application::Run()
    {
        while (m_Running)
        {
            float time = static_cast<float>(glfwGetTime());
            Timestep timestep = time - m_LastFrameTime;
            m_LastFrameTime = time;

            PerformanceMonitor::Get().BeginFrame(time);

            if (!m_Minimized)
            {
                for (const auto& layer : m_LayerStack)
                    layer->OnUpdate(timestep);

                float imguiCpuMs = 0.0f;
                m_ImGuiLayer->Begin();
                {
                    PROFILE_SCOPE("ImGui", &imguiCpuMs);
                    for (const auto& layer : m_LayerStack)
                        layer->OnImGuiRender();
                }
                PerformanceMonitor::Get().SetImGuiCPU(imguiCpuMs);
                m_ImGuiLayer->End();
            }

            m_Window->OnUpdate();

            PerformanceMonitor::Get().EndFrame();

            if (m_FrameRateLimitEnabled && !m_Window->IsVSync() && m_TargetFrameRate > 0.0f)
            {
                float targetFrameTime = 1.0f / m_TargetFrameRate;
                float elapsed = static_cast<float>(glfwGetTime()) - m_LastFrameTime;
                float sleepTime = targetFrameTime - elapsed;
                if (sleepTime > 0.001f)
                    std::this_thread::sleep_for(std::chrono::duration<float>(sleepTime - 0.001f));
            }
        }
    }

} // namespace Engine