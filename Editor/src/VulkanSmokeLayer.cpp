#include "VulkanSmokeLayer.h"

#include "Core/Application.h"
#include "Core/Log.h"
#include "Renderer/RenderCommand.h"

#include <imgui.h>

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

    void VulkanSmokeLayer::OnImGuiRender()
    {
        // Phase 6 视觉验证：在 Vulkan smoke layer 上叠加 ImGui 内容，确认 RenderImGui pass 工作
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        if (ImGui::Begin("Vulkan ImGui 集成验证"))
        {
            ImGui::Text("如果你能看到这个窗口，Phase 6 ImGui Vulkan 集成已生效。");
            ImGui::Separator();
            ImGui::Text("中文字体 (NotoSansSC) - 测试");
            ImGui::Text("DockSpace、拖动、Resize 都应可用。");
            ImGui::Text("启用 Viewports：把本窗口拖出主窗口外验证多视口。");
        }
        ImGui::End();

        ImGui::ShowDemoWindow();
    }

} // namespace Engine
