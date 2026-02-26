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

namespace Engine
{

    Application* Application::s_Instance = nullptr;

    Application::Application()
    {
        ENGINE_CORE_ASSERT(!s_Instance, "Application already exists!");
        s_Instance = this;

        WindowProps props;
        m_Window = std::unique_ptr<Window>(Window::Create(props));

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

        m_ImGuiLayer = new ImGuiLayer();
        PushOverlay(m_ImGuiLayer);
    }

    Application::~Application()
    {
        PerformanceMonitor::Get().Shutdown();
        AssetManager::Shutdown();
        Renderer::Shutdown();
        s_Instance = nullptr;
    }

    void Application::PushLayer(Layer* layer)
    {
        m_LayerStack.PushLayer(layer);
        layer->OnAttach();
    }

    void Application::PushOverlay(Layer* overlay)
    {
        m_LayerStack.PushOverlay(overlay);
        overlay->OnAttach();
    }

    void Application::OnEvent(Event& e)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& e) { return OnWindowClose(e); });
        dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& e) { return OnWindowResize(e); });

        for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
        {
            if (e.Handled)
            {
                break;
            }
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
                for (Layer* layer : m_LayerStack)
                {
                    layer->OnUpdate(timestep);
                }

                float imguiCpuMs = 0.0f;
                m_ImGuiLayer->Begin();
                {
                    PROFILE_SCOPE("ImGui", &imguiCpuMs);
                    for (Layer* layer : m_LayerStack)
                    {
                        layer->OnImGuiRender();
                    }
                }
                PerformanceMonitor::Get().SetImGuiCPU(imguiCpuMs);
                m_ImGuiLayer->End();
            }

            PerformanceMonitor::Get().EndFrame();

            m_Window->OnUpdate();
        }
    }

} // namespace Engine
