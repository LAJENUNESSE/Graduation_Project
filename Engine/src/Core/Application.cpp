#include "engpch.h"
#include "Core/Application.h"
#include "Core/Log.h"
#include "Core/Timestep.h"
#include "Events/ApplicationEvent.h"
#include "ImGui/ImGuiLayer.h"
#include "Renderer/Renderer.h"

#include <GLFW/glfw3.h>

namespace Engine
{

    Application* Application::s_Instance = nullptr;

    Application::Application()
    {
        s_Instance = this;

        WindowProps props;
        m_Window = std::unique_ptr<Window>(Window::Create(props));
        m_Window->SetEventCallback([this](Event& e) { OnEvent(e); });

        Renderer::Init();

        m_ImGuiLayer = new ImGuiLayer();
        PushOverlay(m_ImGuiLayer);
    }

    Application::~Application()
    {
        Renderer::Shutdown();
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

            if (!m_Minimized)
            {
                for (Layer* layer : m_LayerStack)
                {
                    layer->OnUpdate(timestep);
                }

                m_ImGuiLayer->Begin();
                for (Layer* layer : m_LayerStack)
                {
                    layer->OnImGuiRender();
                }
                m_ImGuiLayer->End();
            }

            m_Window->OnUpdate();
        }
    }

} // namespace Engine
