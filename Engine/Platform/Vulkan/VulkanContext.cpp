#include "engpch.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanAllocator.h"
#include "Platform/Vulkan/VulkanPipeline.h"
#include "Platform/Vulkan/VulkanRenderPass.h"
#include "Platform/Vulkan/VulkanSynchronization.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Platform/Vulkan/DebugTriangleFragSpv.h"
#include "Platform/Vulkan/DebugTriangleVertSpv.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <set>
#include <string>

namespace Engine
{

    VulkanContext* VulkanContext::s_Instance = nullptr;

    // =========================================================================
    // Validation layer callback
    // =========================================================================

    static const std::vector<const char*> s_ValidationLayers = {"VK_LAYER_KHRONOS_validation"};

    static const std::vector<const char*> s_DeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    static VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* code, size_t codeSize)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = codeSize;
        createInfo.pCode    = code;

        VkShaderModule module = VK_NULL_HANDLE;
        VkResult       result = vkCreateShaderModule(device, &createInfo, nullptr, &module);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan shader module!");
        return module;
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
                                                              VkDebugUtilsMessageTypeFlagsEXT             type,
                                                              const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                                                              void*                                       userData)
    {
        (void)type;
        (void)userData;
        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            ENGINE_CORE_ERROR("[Vulkan Validation] {}", callbackData->pMessage);
        else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            ENGINE_CORE_WARN("[Vulkan Validation] {}", callbackData->pMessage);
        else
            ENGINE_CORE_TRACE("[Vulkan Validation] {}", callbackData->pMessage);
        return VK_FALSE;
    }

    // =========================================================================
    // Dynamic function loading helpers
    // =========================================================================

    static VkResult CreateDebugUtilsMessengerEXT(VkInstance                                instance,
                                                 const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                                 const VkAllocationCallbacks*              pAllocator,
                                                 VkDebugUtilsMessengerEXT*                 pDebugMessenger)
    {
        auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        return func ? func(instance, pCreateInfo, pAllocator, pDebugMessenger) : VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    static void DestroyDebugUtilsMessengerEXT(VkInstance                   instance,
                                              VkDebugUtilsMessengerEXT     debugMessenger,
                                              const VkAllocationCallbacks* pAllocator)
    {
        auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (func)
            func(instance, debugMessenger, pAllocator);
    }

    // =========================================================================
    // Constructor / Destructor
    // =========================================================================

    VulkanContext::VulkanContext(GLFWwindow* windowHandle) : m_WindowHandle(windowHandle)
    {
        ENGINE_CORE_RELEASE_ASSERT(windowHandle, "Window handle is null!");
    }

    VulkanContext::~VulkanContext()
    {
        Cleanup();
    }

    void VulkanContext::DeferDestroy(std::function<void(VkDevice)>&& fn)
    {
        VulkanContext* context = Get();
        if (!context)
            return;

        context->m_DeletionQueue.Submit(context->m_CurrentFrame, std::move(fn));
    }

    void VulkanContext::FlushDeferredDestructions()
    {
        if (m_Device == VK_NULL_HANDLE)
            return;

        vkDeviceWaitIdle(m_Device);
        m_DeletionQueue.FlushAll(m_Device);
    }

    // =========================================================================
    // Init
    // =========================================================================

    void VulkanContext::Init()
    {
        CreateInstance();
        SetupDebugMessenger();
        CreateSurface();
        PickPhysicalDevice();
        CreateLogicalDevice();
        VulkanAllocator::Init(m_Instance, m_PhysicalDevice, m_Device);
        CreateSwapchain();
        CreateCommandPool();
        CreateSyncObjects();
        CreateImGuiRenderPass();
        CreateImGuiFramebuffers();
        CreateDebugDrawResources();
        CreateDefaultSampler();

        // 注意：dispatcher 的占位纹理创建依赖 VulkanContext::Get()，必须在单例注册之后
        s_Instance = this;
        m_SceneDrawDispatcher.Init(m_Device);

        ENGINE_CORE_INFO("Vulkan context initialized successfully");
    }

    void VulkanContext::QueueDrawArrays(uint32_t count, uint32_t firstVertex)
    {
        if (count == 0)
            return;

        m_PendingDrawCalls.push_back({count, firstVertex, 1, DebugPrimitiveType::Triangles});
    }

    void VulkanContext::QueueDrawArraysInstanced(uint32_t count, uint32_t instanceCount, uint32_t firstVertex)
    {
        if (count == 0 || instanceCount == 0)
            return;

        m_PendingDrawCalls.push_back({count, firstVertex, instanceCount, DebugPrimitiveType::Triangles});
    }

    void VulkanContext::QueueDrawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset)
    {
        (void)vertexOffset;

        if (indexCount == 0)
            return;

        m_PendingDrawCalls.push_back({indexCount, firstIndex, 1, DebugPrimitiveType::Triangles});
    }

    void VulkanContext::QueueDrawLines(uint32_t count, uint32_t firstVertex)
    {
        if (count == 0)
            return;

        m_PendingDrawCalls.push_back({count, firstVertex, 1, DebugPrimitiveType::Lines});
    }

    // =========================================================================
    // BeginFrame / EndFrame — 高级帧录制接口
    // =========================================================================

    bool VulkanContext::BeginFrame()
    {
        ENGINE_CORE_RELEASE_ASSERT(!m_FrameInProgress, "VulkanContext::BeginFrame called twice without EndFrame!");

        // Wait for previous frame at this index
        vkWaitForFences(m_Device, 1, &m_InFlightFences[m_CurrentFrame], VK_TRUE, UINT64_MAX);

        // fence 已确认该槽位上一轮 GPU 工作完成：此刻销毁挂起的资源绝对安全
        m_DeletionQueue.FlushSlot(m_CurrentFrame, m_Device);

        // Acquire next swapchain image；image 可用后 signal m_AcquireFence，
        // host 等待替代原 acquire 信号量的 GPU 侧等待，规避 swapchain
        // semaphore 复用规则
        VkResult result = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, VK_NULL_HANDLE, m_AcquireFence,
                                                &m_PendingImageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            RecreateSwapchain();
            return false;
        }
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR,
                                   "Failed to acquire swapchain image!");

        // 确认 image 可用后立即复位 fence，保证后续帧可复用
        vkWaitForFences(m_Device, 1, &m_AcquireFence, VK_TRUE, UINT64_MAX);
        vkResetFences(m_Device, 1, &m_AcquireFence);

        vkResetFences(m_Device, 1, &m_InFlightFences[m_CurrentFrame]);

        // 帧首清空场景纹理槽（上一帧槽位对新一帧无意义，防悬垂引用被复用）
        m_SceneState.ResetTextureSlots();
        m_SceneDrawDispatcher.OnBeginFrame(m_CurrentFrame);

        VulkanCommandBuffer commandBuffer(m_CommandBuffers[m_CurrentFrame]);
        commandBuffer.Reset();
        commandBuffer.Begin();

        m_FrameInProgress = true;
        return true;
    }

    void VulkanContext::EndFrame()
    {
        if (!m_FrameInProgress)
            return;

        VkCommandBuffer     cmd = m_CommandBuffers[m_CurrentFrame];
        VulkanCommandBuffer commandBuffer(cmd);
        commandBuffer.End();

        // Submit：image 可用性已由 m_AcquireFence 在 host 侧确认，无需再等
        // 信号量；renderFinished 按 present 目标 image 一比一取用
        VkSemaphore signalSemaphore = m_RenderFinishedSemaphores[m_PendingImageIndex];

        VkSubmitInfo submitInfo{};
        submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount   = 1;
        submitInfo.pCommandBuffers      = &cmd;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores    = &signalSemaphore;

        VkResult submitResult = vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_InFlightFences[m_CurrentFrame]);
        ENGINE_CORE_RELEASE_ASSERT(submitResult == VK_SUCCESS, "Failed to submit draw command buffer!");

        // Readback fence 信号化：本帧主 submit 已排队，追加零 cmd submit 在同一队列后面
        // 顺序保证 cmdCopyBuffer 已完成 → host-visible staging 可读
        for (VkFence fence : m_PendingReadbackFences)
        {
            VkSubmitInfo emptySubmit{};
            emptySubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            vkQueueSubmit(m_GraphicsQueue, 1, &emptySubmit, fence);
        }
        m_PendingReadbackFences.clear();

        // Present：等待的信号量与 submit signal 的是同一个 per-image renderFinished
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores    = &signalSemaphore;
        presentInfo.swapchainCount     = 1;
        presentInfo.pSwapchains        = &m_Swapchain;
        presentInfo.pImageIndices      = &m_PendingImageIndex;

        VkResult result = vkQueuePresentKHR(m_PresentQueue, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_FramebufferResized)
        {
            m_FramebufferResized = false;
            RecreateSwapchain();
        }

        m_CurrentFrame    = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        m_FrameInProgress = false;
    }

    void VulkanContext::RegisterReadbackFenceSignal(VkFence fence)
    {
        if (fence != VK_NULL_HANDLE)
            m_PendingReadbackFences.push_back(fence);
    }

    // =========================================================================
    // SwapBuffers — Vulkan path 下退化为 no-op；帧生命周期由主循环
    // (Application::Run) 通过 BeginRenderFrame / EndRenderFrame 显式驱动。
    // Window::OnUpdate 仍调本函数（OpenGL path 需要 glfwSwapBuffers），不再做帧录制。
    // =========================================================================

    void VulkanContext::SwapBuffers() {}

    void VulkanContext::BeginRenderFrame()
    {
        BeginFrame(); // 内部 m_FrameInProgress 跟踪 swapchain recreate 情况
    }

    void VulkanContext::EndRenderFrame()
    {
        if (!m_FrameInProgress)
            return;

        VkCommandBuffer cmd        = m_CommandBuffers[m_CurrentFrame];
        const uint32_t  imageIndex = m_PendingImageIndex;

        RecordDefaultFramePasses(cmd, imageIndex);
        EndFrame();
    }

    void VulkanContext::RecordDefaultFramePasses(VkCommandBuffer cmd, uint32_t imageIndex)
    {
        const bool hasPendingDraws = !m_PendingDrawCalls.empty();
        const bool canDebugDraw    = hasPendingDraws && m_DebugRenderPass != VK_NULL_HANDLE &&
                                  m_DebugPipeline != VK_NULL_HANDLE && m_DebugLinePipeline != VK_NULL_HANDLE &&
                                  imageIndex < m_DebugFramebuffers.size();

        if (canDebugDraw)
        {
            VkClearValue clearValue{};
            clearValue.color = {{m_ClearColor.r, m_ClearColor.g, m_ClearColor.b, m_ClearColor.a}};

            VkRenderPassBeginInfo renderPassBegin{};
            renderPassBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassBegin.renderPass        = m_DebugRenderPass;
            renderPassBegin.framebuffer       = m_DebugFramebuffers[imageIndex];
            renderPassBegin.renderArea.offset = {0, 0};
            renderPassBegin.renderArea.extent = m_SwapchainExtent;
            renderPassBegin.clearValueCount   = 1;
            renderPassBegin.pClearValues      = &clearValue;

            vkCmdBeginRenderPass(cmd, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);

            const uint32_t viewportX         = std::min(m_ViewportX, m_SwapchainExtent.width);
            const uint32_t viewportY         = std::min(m_ViewportY, m_SwapchainExtent.height);
            const uint32_t maxViewportWidth  = m_SwapchainExtent.width - viewportX;
            const uint32_t maxViewportHeight = m_SwapchainExtent.height - viewportY;
            const uint32_t viewportWidth =
                (m_ViewportWidth > 0) ? std::min(m_ViewportWidth, maxViewportWidth) : maxViewportWidth;
            const uint32_t viewportHeight =
                (m_ViewportHeight > 0) ? std::min(m_ViewportHeight, maxViewportHeight) : maxViewportHeight;

            VkViewport viewport{};
            viewport.x        = static_cast<float>(viewportX);
            viewport.y        = static_cast<float>(viewportY);
            viewport.width    = static_cast<float>(viewportWidth);
            viewport.height   = static_cast<float>(viewportHeight);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {static_cast<int32_t>(viewportX), static_cast<int32_t>(viewportY)};
            scissor.extent = {viewportWidth, viewportHeight};
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            VkPipeline boundPipeline = VK_NULL_HANDLE;
            for (const auto& drawCall : m_PendingDrawCalls)
            {
                if (drawCall.VertexCount == 0 || drawCall.InstanceCount == 0)
                    continue;

                const VkPipeline targetPipeline =
                    (drawCall.Primitive == DebugPrimitiveType::Lines) ? m_DebugLinePipeline : m_DebugPipeline;
                if (targetPipeline == VK_NULL_HANDLE)
                    continue;

                if (boundPipeline != targetPipeline)
                {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, targetPipeline);
                    boundPipeline = targetPipeline;
                }

                vkCmdDraw(cmd, drawCall.VertexCount, drawCall.InstanceCount, drawCall.FirstVertex, 0);
            }

            vkCmdEndRenderPass(cmd);
        }
        else
        {
            // Transition: UNDEFINED -> TRANSFER_DST
            {
                VkImageMemoryBarrier barrier{};
                barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                barrier.image                           = m_SwapchainImages[imageIndex];
                barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel   = 0;
                barrier.subresourceRange.levelCount     = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount     = 1;
                barrier.srcAccessMask                   = 0;
                barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;

                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                     nullptr, 0, nullptr, 1, &barrier);
            }

            VkClearColorValue       clearColor = {{m_ClearColor.r, m_ClearColor.g, m_ClearColor.b, m_ClearColor.a}};
            VkImageSubresourceRange range{};
            range.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            range.baseMipLevel   = 0;
            range.levelCount     = 1;
            range.baseArrayLayer = 0;
            range.layerCount     = 1;
            vkCmdClearColorImage(cmd, m_SwapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor,
                                 1, &range);

            // Transition: TRANSFER_DST -> COLOR_ATTACHMENT_OPTIMAL (handed off to ImGui pass)
            {
                VkImageMemoryBarrier barrier{};
                barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout                       = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                barrier.image                           = m_SwapchainImages[imageIndex];
                barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel   = 0;
                barrier.subresourceRange.levelCount     = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount     = 1;
                barrier.srcAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                     0, 0, nullptr, 0, nullptr, 1, &barrier);
            }

            if (hasPendingDraws)
            {
                static bool warnedDrawFallback = false;
                if (!warnedDrawFallback)
                {
                    warnedDrawFallback = true;
                    ENGINE_CORE_WARN("[Vulkan] Debug draw pipeline unavailable, falling back to clear-only path");
                }
            }
        }

        m_PendingDrawCalls.clear();

        // ImGui pass: 始终录制（drawData 为空时只做 layout transition: COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR）
        RecordImGuiPass(cmd, imageIndex);
    }

    // =========================================================================
    // Instance creation
    // =========================================================================

    void VulkanContext::CreateInstance()
    {
        // Check validation layer support
        if (s_EnableValidation)
        {
            uint32_t layerCount = 0;
            vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
            std::vector<VkLayerProperties> available(layerCount);
            vkEnumerateInstanceLayerProperties(&layerCount, available.data());

            for (const char* name : s_ValidationLayers)
            {
                bool found = false;
                for (const auto& prop : available)
                {
                    if (std::string(prop.layerName) == name)
                    {
                        found = true;
                        break;
                    }
                }
                ENGINE_CORE_RELEASE_ASSERT(found, "Vulkan validation layer not available: {0}");
            }
        }

        VkApplicationInfo appInfo{};
        appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName   = "GameEngine Editor";
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.pEngineName        = "GameEngine";
        appInfo.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
        appInfo.apiVersion         = VK_API_VERSION_1_2;

        // Required extensions: GLFW surface + debug utils
        uint32_t                 glfwExtCount = 0;
        const char**             glfwExts     = glfwGetRequiredInstanceExtensions(&glfwExtCount);
        std::vector<const char*> extensions(glfwExts, glfwExts + glfwExtCount);

        if (s_EnableValidation)
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        VkInstanceCreateInfo createInfo{};
        createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo        = &appInfo;
        createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        if (s_EnableValidation)
        {
            createInfo.enabledLayerCount   = static_cast<uint32_t>(s_ValidationLayers.size());
            createInfo.ppEnabledLayerNames = s_ValidationLayers.data();

            debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            debugCreateInfo.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debugCreateInfo.pfnUserCallback = VulkanDebugCallback;
            createInfo.pNext                = &debugCreateInfo;
        }

        VkResult result = vkCreateInstance(&createInfo, nullptr, &m_Instance);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan instance!");

        ENGINE_CORE_INFO("Vulkan instance created (API 1.2)");
    }

    // =========================================================================
    // Debug messenger
    // =========================================================================

    void VulkanContext::SetupDebugMessenger()
    {
        if (!s_EnableValidation)
            return;

        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = VulkanDebugCallback;

        VkResult result = CreateDebugUtilsMessengerEXT(m_Instance, &createInfo, nullptr, &m_DebugMessenger);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to set up Vulkan debug messenger!");
    }

    // =========================================================================
    // Surface
    // =========================================================================

    void VulkanContext::CreateSurface()
    {
        VkResult result = glfwCreateWindowSurface(m_Instance, m_WindowHandle, nullptr, &m_Surface);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan window surface!");
    }

    // =========================================================================
    // Physical device selection
    // =========================================================================

    void VulkanContext::PickPhysicalDevice()
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
        ENGINE_CORE_RELEASE_ASSERT(deviceCount > 0, "Failed to find GPUs with Vulkan support!");

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

        // Pick first discrete GPU, or fallback to first device
        for (const auto& dev : devices)
        {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(dev, &props);

            // Check queue families
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, nullptr);
            std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, queueFamilies.data());

            bool     hasGraphics = false, hasPresent = false, hasCompute = false;
            uint32_t gfxFamily = 0, presentFamily = 0, computeFamily = 0;
            bool     computeIsDedicated = false; // 优先 async compute（无 GRAPHICS bit）

            for (uint32_t i = 0; i < queueFamilyCount; i++)
            {
                const VkQueueFlags flags = queueFamilies[i].queueFlags;

                if (flags & VK_QUEUE_GRAPHICS_BIT)
                {
                    gfxFamily   = i;
                    hasGraphics = true;
                }

                // Compute family 选择策略：
                // 1. 优先无 GRAPHICS bit 的（async compute，可与 graphics 并发）
                // 2. 否则任意带 COMPUTE bit 的（graphics family 必然带 COMPUTE bit）
                if (flags & VK_QUEUE_COMPUTE_BIT)
                {
                    const bool dedicated = !(flags & VK_QUEUE_GRAPHICS_BIT);
                    if (!hasCompute || (dedicated && !computeIsDedicated))
                    {
                        computeFamily      = i;
                        hasCompute         = true;
                        computeIsDedicated = dedicated;
                    }
                }

                VkBool32 presentSupport = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, m_Surface, &presentSupport);
                if (presentSupport)
                {
                    presentFamily = i;
                    hasPresent    = true;
                }
            }

            // Check device extension support
            uint32_t extCount = 0;
            vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
            std::vector<VkExtensionProperties> availableExts(extCount);
            vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, availableExts.data());

            std::set<std::string> requiredExts(s_DeviceExtensions.begin(), s_DeviceExtensions.end());
            for (const auto& ext : availableExts)
                requiredExts.erase(ext.extensionName);

            if (hasGraphics && hasPresent && hasCompute && requiredExts.empty())
            {
                m_PhysicalDevice      = dev;
                m_GraphicsQueueFamily = gfxFamily;
                m_PresentQueueFamily  = presentFamily;
                m_ComputeQueueFamily  = computeFamily;

                ENGINE_CORE_INFO("Vulkan physical device: {}", props.deviceName);
                ENGINE_CORE_INFO("Vulkan queue families: graphics={}, present={}, compute={} ({})", gfxFamily,
                                 presentFamily, computeFamily,
                                 computeIsDedicated ? "dedicated" : "shared with graphics");
                return;
            }
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Failed to find a suitable Vulkan GPU!");
    }

    // =========================================================================
    // Logical device + queues
    // =========================================================================

    void VulkanContext::CreateLogicalDevice()
    {
        std::set<uint32_t> uniqueFamilies = {m_GraphicsQueueFamily, m_PresentQueueFamily, m_ComputeQueueFamily};
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        float                                queuePriority = 1.0f;

        for (uint32_t family : uniqueFamilies)
        {
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = family;
            queueInfo.queueCount       = 1;
            queueInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures{};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount    = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos       = queueCreateInfos.data();
        createInfo.pEnabledFeatures        = &deviceFeatures;
        createInfo.enabledExtensionCount   = static_cast<uint32_t>(s_DeviceExtensions.size());
        createInfo.ppEnabledExtensionNames = s_DeviceExtensions.data();

        if (s_EnableValidation)
        {
            createInfo.enabledLayerCount   = static_cast<uint32_t>(s_ValidationLayers.size());
            createInfo.ppEnabledLayerNames = s_ValidationLayers.data();
        }

        VkResult result = vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan logical device!");

        vkGetDeviceQueue(m_Device, m_GraphicsQueueFamily, 0, &m_GraphicsQueue);
        vkGetDeviceQueue(m_Device, m_PresentQueueFamily, 0, &m_PresentQueue);
        vkGetDeviceQueue(m_Device, m_ComputeQueueFamily, 0, &m_ComputeQueue);
    }

    // =========================================================================
    // Swapchain
    // =========================================================================

    void VulkanContext::CreateSwapchain()
    {
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &capabilities);

        // Pick surface format
        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, formats.data());

        VkSurfaceFormatKHR surfaceFormat = formats[0];
        for (const auto& f : formats)
        {
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                surfaceFormat = f;
                break;
            }
        }

        // Pick present mode (prefer mailbox for low latency, fallback FIFO)
        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &presentModeCount, nullptr);
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &presentModeCount, presentModes.data());

        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
        for (const auto& mode : presentModes)
        {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                presentMode = mode;
                break;
            }
        }

        // Pick extent
        VkExtent2D extent;
        if (capabilities.currentExtent.width != UINT32_MAX)
        {
            extent = capabilities.currentExtent;
        }
        else
        {
            int w, h;
            glfwGetFramebufferSize(m_WindowHandle, &w, &h);
            extent.width  = std::clamp(static_cast<uint32_t>(w), capabilities.minImageExtent.width,
                                       capabilities.maxImageExtent.width);
            extent.height = std::clamp(static_cast<uint32_t>(h), capabilities.minImageExtent.height,
                                       capabilities.maxImageExtent.height);
        }

        uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
            imageCount = capabilities.maxImageCount;

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface          = m_Surface;
        createInfo.minImageCount    = imageCount;
        createInfo.imageFormat      = surfaceFormat.format;
        createInfo.imageColorSpace  = surfaceFormat.colorSpace;
        createInfo.imageExtent      = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        uint32_t queueFamilyIndices[] = {m_GraphicsQueueFamily, m_PresentQueueFamily};
        if (m_GraphicsQueueFamily != m_PresentQueueFamily)
        {
            createInfo.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices   = queueFamilyIndices;
        }
        else
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform   = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode    = presentMode;
        createInfo.clipped        = VK_TRUE;
        createInfo.oldSwapchain   = VK_NULL_HANDLE;

        VkResult result = vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &m_Swapchain);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan swapchain!");

        // Get swapchain images
        uint32_t actualCount = 0;
        vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &actualCount, nullptr);
        m_SwapchainImages.resize(actualCount);
        vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &actualCount, m_SwapchainImages.data());

        m_SwapchainImageViews.resize(actualCount);
        for (uint32_t i = 0; i < actualCount; ++i)
        {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image                           = m_SwapchainImages[i];
            viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format                          = surfaceFormat.format;
            viewInfo.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel   = 0;
            viewInfo.subresourceRange.levelCount     = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount     = 1;

            VkResult viewResult = vkCreateImageView(m_Device, &viewInfo, nullptr, &m_SwapchainImageViews[i]);
            ENGINE_CORE_RELEASE_ASSERT(viewResult == VK_SUCCESS, "Failed to create swapchain image view!");
        }

        m_SwapchainFormat          = surfaceFormat.format;
        m_SwapchainExtent          = extent;
        m_SwapchainInfo.ImageCount = actualCount;
        if (m_ViewportWidth == 0 || m_ViewportHeight == 0)
        {
            m_ViewportX      = 0;
            m_ViewportY      = 0;
            m_ViewportWidth  = extent.width;
            m_ViewportHeight = extent.height;
        }

        ENGINE_CORE_INFO("Vulkan swapchain created ({}x{}, {} images, format {})", extent.width, extent.height,
                         actualCount, static_cast<int>(surfaceFormat.format));
    }

    // =========================================================================
    // Command pool + buffers
    // =========================================================================

    void VulkanContext::CreateCommandPool()
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = m_GraphicsQueueFamily;

        VkResult result = vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan command pool!");

        m_CommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = m_CommandPool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

        result = vkAllocateCommandBuffers(m_Device, &allocInfo, m_CommandBuffers.data());
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to allocate Vulkan command buffers!");
    }

    // =========================================================================
    // Synchronization objects
    // =========================================================================

    void VulkanContext::CreateSyncObjects()
    {
        // acquire 用 host 等待的 fence；首次使用前必须处于 unsignaled
        m_AcquireFence = VulkanSynchronization::CreateFence(m_Device, false);

        m_InFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            m_InFlightFences[i] = VulkanSynchronization::CreateFence(m_Device, true);

        CreateImageSemaphores();
    }

    void VulkanContext::CreateImageSemaphores()
    {
        const uint32_t imageCount = static_cast<uint32_t>(m_SwapchainImages.size());
        m_RenderFinishedSemaphores.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; i++)
            m_RenderFinishedSemaphores[i] = VulkanSynchronization::CreateSemaphore(m_Device);
    }

    void VulkanContext::DestroyImageSemaphores()
    {
        for (VkSemaphore& semaphore : m_RenderFinishedSemaphores)
            VulkanSynchronization::DestroySemaphore(m_Device, semaphore);
        m_RenderFinishedSemaphores.clear();
    }

    // =========================================================================
    // ImGui Render Pass
    // =========================================================================

    void VulkanContext::CreateImGuiRenderPass()
    {
        VulkanColorRenderPassDesc desc{};
        desc.ColorFormat   = m_SwapchainFormat;
        desc.LoadOp        = VK_ATTACHMENT_LOAD_OP_LOAD;
        desc.StoreOp       = VK_ATTACHMENT_STORE_OP_STORE;
        desc.InitialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        desc.FinalLayout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        m_ImGuiRenderPass = VulkanRenderPass::CreateColorOnly(m_Device, desc);
    }

    void* VulkanContext::GetImGuiTextureForView(void* imageView)
    {
        if (imageView == nullptr)
            return nullptr;

        auto it = m_ImGuiTextures.find(imageView);
        if (it != m_ImGuiTextures.end())
            return it->second;

        const VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(m_DefaultSampler, static_cast<VkImageView>(imageView),
                                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        ENGINE_CORE_RELEASE_ASSERT(set != VK_NULL_HANDLE, "ImGui_ImplVulkan_AddTexture failed");
        m_ImGuiTextures[imageView] = set;
        return set;
    }

    void VulkanContext::RemoveImGuiTexture(void* imageView)
    {
        auto it = m_ImGuiTextures.find(imageView);
        if (it == m_ImGuiTextures.end())
            return;

        const VkDescriptorSet descriptorSet = it->second;
        m_ImGuiTextures.erase(it);

        if (m_FrameInProgress)
        {
            // 旧视口 descriptor 可能已经被当前帧或前一帧的 command buffer 引用。
            // 等当前帧槽位 fence 完成后再交给 ImGui backend 释放，避免
            // VUID-vkFreeDescriptorSets-pDescriptorSets-00309。
            DeferDestroy([descriptorSet](VkDevice) { ImGui_ImplVulkan_RemoveTexture(descriptorSet); });
        }
        else
        {
            // 非录制阶段也可能有已提交的 GPU 工作，先等待再立即释放。
            vkDeviceWaitIdle(m_Device);
            ImGui_ImplVulkan_RemoveTexture(descriptorSet);
        }
    }

    // Phase 8.2：场景 descriptor 共用默认采样器（linear + clampToEdge）。
    // 深度图/IBL/材质纹理在 BindTextureView 未显式携带 sampler 时回退到它。
    void VulkanContext::CreateDefaultSampler()
    {
        VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter        = VK_FILTER_LINEAR;
        samplerInfo.minFilter        = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxLod           = VK_LOD_CLAMP_NONE;

        const VkResult result = vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_DefaultSampler);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create default sampler");
    }

    void VulkanContext::CreateDebugDrawResources()
    {
        DestroyDebugDrawResources();

        if (m_SwapchainImageViews.empty())
            return;

        VulkanColorRenderPassDesc desc{};
        desc.ColorFormat   = m_SwapchainFormat;
        desc.LoadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
        desc.StoreOp       = VK_ATTACHMENT_STORE_OP_STORE;
        desc.InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        desc.FinalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        m_DebugRenderPass = VulkanRenderPass::CreateColorOnly(m_Device, desc);

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

        VkResult pipelineLayoutResult =
            vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_DebugPipelineLayout);
        ENGINE_CORE_RELEASE_ASSERT(pipelineLayoutResult == VK_SUCCESS, "Failed to create debug draw pipeline layout!");

        VkShaderModule vertModule =
            CreateShaderModule(m_Device, g_DebugTriangleVertSpv, sizeof(g_DebugTriangleVertSpv));
        VkShaderModule fragModule =
            CreateShaderModule(m_Device, g_DebugTriangleFragSpv, sizeof(g_DebugTriangleFragSpv));

        VkPipelineShaderStageCreateInfo shaderStages[2]{};
        shaderStages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = vertModule;
        shaderStages[0].pName  = "main";

        shaderStages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = fragModule;
        shaderStages[1].pName  = "main";

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport{};
        viewport.x        = 0.0f;
        viewport.y        = 0.0f;
        viewport.width    = static_cast<float>(m_SwapchainExtent.width);
        viewport.height   = static_cast<float>(m_SwapchainExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = m_SwapchainExtent;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports    = &viewport;
        viewportState.scissorCount  = 1;
        viewportState.pScissors     = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable        = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode                = VK_CULL_MODE_NONE;
        rasterizer.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable         = VK_FALSE;
        rasterizer.lineWidth               = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.sampleShadingEnable  = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable   = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments    = &colorBlendAttachment;

        VkDynamicState                   dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates    = dynamicStates;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount          = 2;
        pipelineInfo.pStages             = shaderStages;
        pipelineInfo.pVertexInputState   = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState      = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState   = &multisampling;
        pipelineInfo.pColorBlendState    = &colorBlending;
        pipelineInfo.pDynamicState       = &dynamicState;
        pipelineInfo.layout              = m_DebugPipelineLayout;
        pipelineInfo.renderPass          = m_DebugRenderPass;
        pipelineInfo.subpass             = 0;

        m_DebugPipeline        = VulkanPipeline::CreateGraphics(m_Device, pipelineInfo);
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        m_DebugLinePipeline    = VulkanPipeline::CreateGraphics(m_Device, pipelineInfo);
        vkDestroyShaderModule(m_Device, fragModule, nullptr);
        vkDestroyShaderModule(m_Device, vertModule, nullptr);

        m_DebugFramebuffers.resize(m_SwapchainImageViews.size());
        for (size_t i = 0; i < m_SwapchainImageViews.size(); ++i)
        {
            VkImageView attachments[] = {m_SwapchainImageViews[i]};

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass      = m_DebugRenderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments    = attachments;
            framebufferInfo.width           = m_SwapchainExtent.width;
            framebufferInfo.height          = m_SwapchainExtent.height;
            framebufferInfo.layers          = 1;

            VkResult framebufferResult =
                vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr, &m_DebugFramebuffers[i]);
            ENGINE_CORE_RELEASE_ASSERT(framebufferResult == VK_SUCCESS, "Failed to create debug draw framebuffer!");
        }
    }

    void VulkanContext::DestroyDebugDrawResources()
    {
        for (VkFramebuffer framebuffer : m_DebugFramebuffers)
        {
            if (framebuffer != VK_NULL_HANDLE)
                vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
        }
        m_DebugFramebuffers.clear();

        if (m_DebugLinePipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_Device, m_DebugLinePipeline, nullptr);
            m_DebugLinePipeline = VK_NULL_HANDLE;
        }

        if (m_DebugPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_Device, m_DebugPipeline, nullptr);
            m_DebugPipeline = VK_NULL_HANDLE;
        }

        if (m_DebugPipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(m_Device, m_DebugPipelineLayout, nullptr);
            m_DebugPipelineLayout = VK_NULL_HANDLE;
        }

        if (m_DebugRenderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(m_Device, m_DebugRenderPass, nullptr);
            m_DebugRenderPass = VK_NULL_HANDLE;
        }
    }

    // =========================================================================
    // Swapchain recreation
    // =========================================================================

    void VulkanContext::CleanupSwapchain()
    {
        for (VkImageView imageView : m_SwapchainImageViews)
        {
            if (imageView != VK_NULL_HANDLE)
                vkDestroyImageView(m_Device, imageView, nullptr);
        }
        m_SwapchainImageViews.clear();

        if (m_Swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
            m_Swapchain = VK_NULL_HANDLE;
        }
        m_SwapchainImages.clear();
    }

    void VulkanContext::RecreateSwapchain()
    {
        // Handle minimization
        int width = 0, height = 0;
        glfwGetFramebufferSize(m_WindowHandle, &width, &height);
        while (width == 0 || height == 0)
        {
            glfwGetFramebufferSize(m_WindowHandle, &width, &height);
            glfwWaitEvents();
        }

        vkDeviceWaitIdle(m_Device);
        DestroyDebugDrawResources();
        DestroyImGuiFramebuffers();
        DestroyImageSemaphores(); // image count 可能随重建变化
        CleanupSwapchain();
        CreateSwapchain();
        CreateImageSemaphores();

        if (m_ImGuiRenderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(m_Device, m_ImGuiRenderPass, nullptr);
            m_ImGuiRenderPass = VK_NULL_HANDLE;
        }
        CreateImGuiRenderPass();
        CreateImGuiFramebuffers();
        CreateDebugDrawResources();

        // 防御：若 swapchain image count 变化，同步 ImGui backend
        ImGui_ImplVulkan_SetMinImageCount(static_cast<uint32_t>(m_SwapchainImages.size()));
    }

    // =========================================================================
    // Full cleanup
    // =========================================================================

    void VulkanContext::RenderImGui(void* drawData)
    {
        // 仅缓存 drawData 指针；实际录制在 SwapBuffers 中通过 RecordImGuiPass 完成
        m_PendingImGuiDrawData = static_cast<ImDrawData*>(drawData);
    }

    void VulkanContext::RecordImGuiPass(VkCommandBuffer cmd, uint32_t imageIndex)
    {
        if (m_ImGuiRenderPass == VK_NULL_HANDLE || imageIndex >= m_ImGuiFramebuffers.size())
            return;

        VkRenderPassBeginInfo bi{};
        bi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        bi.renderPass        = m_ImGuiRenderPass;
        bi.framebuffer       = m_ImGuiFramebuffers[imageIndex];
        bi.renderArea.offset = {0, 0};
        bi.renderArea.extent = m_SwapchainExtent;
        bi.clearValueCount   = 0; // LOAD_OP_LOAD

        vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
        if (m_PendingImGuiDrawData && m_PendingImGuiDrawData->CmdListsCount > 0)
            ImGui_ImplVulkan_RenderDrawData(m_PendingImGuiDrawData, cmd);
        vkCmdEndRenderPass(cmd);

        m_PendingImGuiDrawData = nullptr;
    }

    void VulkanContext::CreateImGuiFramebuffers()
    {
        DestroyImGuiFramebuffers();
        if (m_ImGuiRenderPass == VK_NULL_HANDLE || m_SwapchainImageViews.empty())
            return;

        m_ImGuiFramebuffers.resize(m_SwapchainImageViews.size(), VK_NULL_HANDLE);
        for (size_t i = 0; i < m_SwapchainImageViews.size(); ++i)
        {
            VkImageView attachments[] = {m_SwapchainImageViews[i]};

            VkFramebufferCreateInfo fb{};
            fb.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass      = m_ImGuiRenderPass;
            fb.attachmentCount = 1;
            fb.pAttachments    = attachments;
            fb.width           = m_SwapchainExtent.width;
            fb.height          = m_SwapchainExtent.height;
            fb.layers          = 1;

            VkResult r = vkCreateFramebuffer(m_Device, &fb, nullptr, &m_ImGuiFramebuffers[i]);
            ENGINE_CORE_RELEASE_ASSERT(r == VK_SUCCESS, "Failed to create ImGui framebuffer!");
        }
    }

    void VulkanContext::DestroyImGuiFramebuffers()
    {
        for (VkFramebuffer fb : m_ImGuiFramebuffers)
        {
            if (fb != VK_NULL_HANDLE)
                vkDestroyFramebuffer(m_Device, fb, nullptr);
        }
        m_ImGuiFramebuffers.clear();
    }

    VkCommandBuffer VulkanContext::BeginSingleTimeCommands()
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool        = m_CommandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(m_Device, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        return commandBuffer;
    }

    void VulkanContext::EndSingleTimeCommands(VkCommandBuffer commandBuffer)
    {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &commandBuffer;

        vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_GraphicsQueue);

        vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &commandBuffer);
    }

    void VulkanContext::Cleanup()
    {
        m_PipelineBuilder.Clear(m_Device);
        m_SceneDrawDispatcher.Shutdown(m_Device);
        for (auto& [view, set] : m_ImGuiTextures)
            ImGui_ImplVulkan_RemoveTexture(set);
        m_ImGuiTextures.clear();
        if (m_DefaultSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_Device, m_DefaultSampler, nullptr);
            m_DefaultSampler = VK_NULL_HANDLE;
        }

        if (m_Device == VK_NULL_HANDLE)
            return;

        s_Instance = nullptr;

        vkDeviceWaitIdle(m_Device);

        // 所有在途工作已完成：清空两个帧槽位上挂起的延迟销毁
        m_DeletionQueue.FlushAll(m_Device);

        VulkanSynchronization::DestroyFence(m_Device, m_AcquireFence);
        for (VkFence& fence : m_InFlightFences)
            VulkanSynchronization::DestroyFence(m_Device, fence);
        m_InFlightFences.clear();
        DestroyImageSemaphores();

        if (m_CommandPool != VK_NULL_HANDLE)
            vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);

        DestroyDebugDrawResources();

        DestroyImGuiFramebuffers();

        if (m_ImGuiRenderPass != VK_NULL_HANDLE)
            vkDestroyRenderPass(m_Device, m_ImGuiRenderPass, nullptr);

        CleanupSwapchain();

        VulkanAllocator::Shutdown();

        vkDestroyDevice(m_Device, nullptr);
        m_Device = VK_NULL_HANDLE;

        if constexpr (s_EnableValidation)
            if (m_DebugMessenger != VK_NULL_HANDLE)
                DestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);

        if (m_Surface != VK_NULL_HANDLE)
            vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);

        if (m_Instance != VK_NULL_HANDLE)
            vkDestroyInstance(m_Instance, nullptr);
    }

} // namespace Engine
