#include "VulkanSmokeLayer.h"

#include "Core/Application.h"
#include "Core/Log.h"
#include "Renderer/RenderCommand.h"

namespace Engine
{

    VulkanSmokeLayer::VulkanSmokeLayer() : Layer("VulkanSmokeLayer") {}

    void VulkanSmokeLayer::OnAttach()
    {
        ENGINE_CORE_INFO("[Vulkan] Vulkan smoke layer attached");

        auto& window = Application::Get().GetWindow();
        RenderCommand::SetClearColor(m_ClearColor);
        RenderCommand::SetViewport(0, 0, window.GetWidth(), window.GetHeight());
    }

    void VulkanSmokeLayer::OnUpdate(Timestep ts)
    {
        (void)ts;

        auto& window = Application::Get().GetWindow();
        RenderCommand::SetViewport(0, 0, window.GetWidth(), window.GetHeight());
        RenderCommand::DrawArrays(3);
    }

} // namespace Engine
