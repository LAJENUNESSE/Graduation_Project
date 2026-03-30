#include "engpch.h"
#include "ImGui/ImGuiLayer.h"
#include "Asset/PathUtils.h"
#include "Core/Application.h"
#include "Core/Assert.h"
#include "Core/Log.h"
#include "Renderer/RendererAPI.h"

// clang-format off
#include <imgui.h>
#include <imgui_impl_glfw.h>
#if defined(ENGINE_ENABLE_VULKAN)
#include <imgui_impl_vulkan.h>
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanSwapchain.h"
#else
#include <imgui_impl_opengl3.h>
#endif
#include <ImGuizmo.h>
// clang-format on

#include <GLFW/glfw3.h>

namespace Engine
{

    ImGuiLayer::ImGuiLayer() : Layer("ImGuiLayer") {}

    void ImGuiLayer::OnAttach()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        // Note: ViewportsEnable disabled — it causes glfwSwapBuffers in ImGuiLayer::End()
        // which adds ~14ms vsync stall to the ImGui CPU timing, distorting perf data.
        // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        // 加载中文字体
        const std::string fontPath =
            PathUtils::ResolvePath((PathUtils::GetEditorAssetRoot() / "fonts" / "NotoSansSC-Regular.ttf")).string();
        ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 18.0f, nullptr,
                                                    io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        if (!font)
        {
            ENGINE_CORE_WARN("Failed to load Chinese font, falling back to default font");
            io.Fonts->AddFontDefault();
        }

        ImGui::StyleColorsDark();

        ImGuiStyle& style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            style.WindowRounding              = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        GLFWwindow* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());

#if defined(ENGINE_ENABLE_VULKAN)
        // Vulkan backend initialization
        auto* vkContext = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(vkContext, "VulkanContext must be initialized before ImGuiLayer");

        ImGui_ImplGlfw_InitForVulkan(window, true);

        ImGui_ImplVulkan_InitInfo initInfo = {};
        initInfo.ApiVersion                = VK_API_VERSION_1_2;
        initInfo.Instance                  = vkContext->GetInstance();
        initInfo.PhysicalDevice            = vkContext->GetPhysicalDevice();
        initInfo.Device                    = vkContext->GetDevice();
        initInfo.QueueFamily               = vkContext->GetGraphicsQueueFamily();
        initInfo.Queue                     = vkContext->GetGraphicsQueue();
        initInfo.DescriptorPoolSize        = 100; // Let ImGui create its own descriptor pool
        initInfo.MinImageCount             = 2;
        initInfo.ImageCount                = vkContext->GetSwapchain()->GetImageCount();
        initInfo.CheckVkResultFn           = [](VkResult err) {
            if (err != VK_SUCCESS)
            {
                ENGINE_CORE_ERROR("ImGui Vulkan Error: {}", static_cast<int>(err));
            }
        };

        // We need a RenderPass for ImGui — create one for the swapchain format
        initInfo.PipelineInfoMain.RenderPass = vkContext->GetImGuiRenderPass();

        ImGui_ImplVulkan_Init(&initInfo);

        ENGINE_CORE_INFO("ImGui initialized with Vulkan backend");
#else
        // OpenGL backend initialization
        ImGui_ImplGlfw_InitForOpenGL(window, false);
        ImGui_ImplOpenGL3_Init("#version 330");
#endif
    }

    void ImGuiLayer::OnDetach()
    {
#if defined(ENGINE_ENABLE_VULKAN)
        auto* vkContext = VulkanContext::Get();
        if (vkContext)
        {
            vkDeviceWaitIdle(vkContext->GetDevice());
        }
        ImGui_ImplVulkan_Shutdown();
#else
        ImGui_ImplOpenGL3_Shutdown();
#endif
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void ImGuiLayer::OnEvent(Event& event)
    {
        if (m_BlockEvents)
        {
            ImGuiIO& io = ImGui::GetIO();
            event.Handled |= event.IsInCategory(EventCategoryMouse) & io.WantCaptureMouse;
            event.Handled |= event.IsInCategory(EventCategoryKeyboard) & io.WantCaptureKeyboard;
        }
    }

    void ImGuiLayer::Begin()
    {
#if defined(ENGINE_ENABLE_VULKAN)
        ImGui_ImplVulkan_NewFrame();
#else
        ImGui_ImplOpenGL3_NewFrame();
#endif
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
    }

    void ImGuiLayer::End()
    {
        ImGuiIO&     io  = ImGui::GetIO();
        Application& app = Application::Get();
        io.DisplaySize =
            ImVec2(static_cast<float>(app.GetWindow().GetWidth()), static_cast<float>(app.GetWindow().GetHeight()));

        ImGui::Render();

#if defined(ENGINE_ENABLE_VULKAN)
        // Vulkan rendering is handled in the main render loop via VulkanContext
        // ImGui_ImplVulkan_RenderDrawData is called from VulkanContext::RenderImGui()
        auto* vkContext = VulkanContext::Get();
        if (vkContext)
        {
            vkContext->RenderImGui(ImGui::GetDrawData());
        }
#else
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow* backupCurrentContext = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backupCurrentContext);
        }
    }

} // namespace Engine
