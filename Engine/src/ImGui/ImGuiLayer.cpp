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
#include <imgui_impl_opengl3.h>
#if defined(ENGINE_ENABLE_VULKAN)
#include <imgui_impl_vulkan.h>
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanSwapchain.h"
#endif
#include <ImGuizmo.h>
// clang-format on

#include <GLFW/glfw3.h>

namespace Engine
{

    ImFont* ImGuiLayer::s_CodeFont = nullptr;

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

        // 加载 UI 默认字体：NotoSansSC，用于界面中文文字
        const std::string fontPath =
            PathUtils::ResolvePath((PathUtils::GetEditorAssetRoot() / "fonts" / "NotoSansSC-Regular.ttf")).string();
        ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 18.0f, nullptr,
                                                    io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        if (!font)
        {
            ENGINE_CORE_WARN("Failed to load Chinese font, falling back to default font");
            io.Fonts->AddFontDefault();
        }

        // 加载代码等宽字体栈：JetBrains Mono（拉丁）+ Sarasa Mono SC（CJK，严格 2×ASCII 宽）
        // 由 ScriptEditorPanel 等代码编辑面板通过 ImGuiLayer::GetCodeFont() 获取；字号 16.0f 与 VS Code / JetBrains IDE
        // 一致
        const std::string jbmonoPath =
            PathUtils::ResolvePath((PathUtils::GetEditorAssetRoot() / "fonts" / "JetBrainsMono-Regular.ttf")).string();
        const std::string sarasaPath =
            PathUtils::ResolvePath((PathUtils::GetEditorAssetRoot() / "fonts" / "SarasaMonoSC-Regular.ttf")).string();

        ImFont* codeFont = io.Fonts->AddFontFromFileTTF(jbmonoPath.c_str(), 16.0f);
        if (codeFont)
        {
            ImFontConfig cfg;
            cfg.MergeMode      = true;
            cfg.PixelSnapH     = true;
            ImFont* sarasaFont = io.Fonts->AddFontFromFileTTF(sarasaPath.c_str(), 16.0f, &cfg,
                                                              io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            if (!sarasaFont)
            {
                ENGINE_CORE_WARN("Failed to load code CJK font (SarasaMonoSC-Regular.ttf); "
                                 "code editor will show CJK characters as tofu boxes");
            }
            s_CodeFont = codeFont;
        }
        else
        {
            ENGINE_CORE_WARN("Failed to load code font (JetBrainsMono-Regular.ttf); "
                             "script editor will fallback to default UI font");
            s_CodeFont = nullptr;
        }

        ImGui::StyleColorsDark();

        // UE5 Style Customization
        ImGuiStyle& style  = ImGui::GetStyle();
        ImVec4*     colors = style.Colors;

        colors[ImGuiCol_Text]                 = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
        colors[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        colors[ImGuiCol_WindowBg]             = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
        colors[ImGuiCol_ChildBg]              = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
        colors[ImGuiCol_PopupBg]              = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
        colors[ImGuiCol_Border]               = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
        colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]              = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        colors[ImGuiCol_FrameBgActive]        = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
        colors[ImGuiCol_TitleBg]              = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
        colors[ImGuiCol_TitleBgActive]        = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
        colors[ImGuiCol_MenuBarBg]            = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
        colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
        colors[ImGuiCol_CheckMark]            = ImVec4(0.00f, 0.45f, 0.85f, 1.00f);
        colors[ImGuiCol_SliderGrab]           = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
        colors[ImGuiCol_Button]               = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
        colors[ImGuiCol_ButtonHovered]        = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        colors[ImGuiCol_ButtonActive]         = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
        colors[ImGuiCol_Header]               = ImVec4(0.00f, 0.35f, 0.62f, 1.00f);
        colors[ImGuiCol_HeaderHovered]        = ImVec4(0.05f, 0.40f, 0.68f, 1.00f);
        colors[ImGuiCol_HeaderActive]         = ImVec4(0.00f, 0.29f, 0.52f, 1.00f);
        colors[ImGuiCol_Separator]            = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
        colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_SeparatorActive]      = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        colors[ImGuiCol_ResizeGrip]           = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
        colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
        colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
        colors[ImGuiCol_Tab]                  = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
        colors[ImGuiCol_TabHovered]           = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        colors[ImGuiCol_TabActive]            = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
        colors[ImGuiCol_TabUnfocused]         = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_DockingPreview]       = ImVec4(0.00f, 0.45f, 0.85f, 0.30f);
        colors[ImGuiCol_DockingEmptyBg]       = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);

        style.WindowPadding    = ImVec2(8.0f, 8.0f);
        style.FramePadding     = ImVec2(5.0f, 4.0f);
        style.CellPadding      = ImVec2(4.0f, 2.0f);
        style.ItemSpacing      = ImVec2(8.0f, 4.0f);
        style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
        style.IndentSpacing    = 20.0f;
        style.ScrollbarSize    = 14.0f;
        style.GrabMinSize      = 10.0f;

        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize  = 1.0f;
        style.PopupBorderSize  = 1.0f;
        style.FrameBorderSize  = 0.0f;
        style.TabBorderSize    = 0.0f;

        style.WindowRounding    = 0.0f;
        style.ChildRounding     = 0.0f;
        style.FrameRounding     = 2.0f;
        style.PopupRounding     = 0.0f;
        style.ScrollbarRounding = 2.0f;
        style.GrabRounding      = 2.0f;
        style.TabRounding       = 2.0f;

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            style.WindowRounding              = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        GLFWwindow* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());

#if defined(ENGINE_ENABLE_VULKAN)
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
        {
            // Vulkan backend initialization
            auto* vkContext = VulkanContext::Get();
            ENGINE_CORE_RELEASE_ASSERT(vkContext, "VulkanContext must be initialized before ImGuiLayer");

            ImGui_ImplGlfw_InitForVulkan(window, false);

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
            initInfo.CheckVkResultFn           = [](VkResult err)
            {
                if (err != VK_SUCCESS)
                {
                    ENGINE_CORE_ERROR("ImGui Vulkan Error: {}", static_cast<int>(err));
                }
            };

            // We need a RenderPass for ImGui — create one for the swapchain format
            initInfo.PipelineInfoMain.RenderPass = vkContext->GetImGuiRenderPass();

            ImGui_ImplVulkan_Init(&initInfo);

            ENGINE_CORE_INFO("ImGui initialized with Vulkan backend");
        }
        else
#endif
        {
            // OpenGL backend initialization
            ImGui_ImplGlfw_InitForOpenGL(window, false);
            ImGui_ImplOpenGL3_Init("#version 330");
        }
    }

    void ImGuiLayer::OnDetach()
    {
#if defined(ENGINE_ENABLE_VULKAN)
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
        {
            auto* vkContext = VulkanContext::Get();
            if (vkContext)
            {
                vkDeviceWaitIdle(vkContext->GetDevice());
            }
            ImGui_ImplVulkan_Shutdown();
        }
        else
#endif
        {
            ImGui_ImplOpenGL3_Shutdown();
        }
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        // 字体由 ImGui context 拥有，context 销毁后指针已失效
        s_CodeFont = nullptr;
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
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
        {
            ImGui_ImplVulkan_NewFrame();
        }
        else
#endif
        {
            ImGui_ImplOpenGL3_NewFrame();
        }
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
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
        {
            // Vulkan rendering is handled in the main render loop via VulkanContext
            // ImGui_ImplVulkan_RenderDrawData is called from VulkanContext::RenderImGui()
            auto* vkContext = VulkanContext::Get();
            if (vkContext)
            {
                vkContext->RenderImGui(ImGui::GetDrawData());
            }
        }
        else
#endif
        {
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow* backupCurrentContext = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backupCurrentContext);
        }
    }

} // namespace Engine
