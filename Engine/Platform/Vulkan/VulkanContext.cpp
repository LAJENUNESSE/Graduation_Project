#include "engpch.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanSwapchain.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"

// clang-format off
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#if defined(ENGINE_ENABLE_VULKAN)
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#endif
// clang-format on

#include "Core/Assert.h"
#include "Core/Log.h"

#include <vector>

namespace Engine
{

    VulkanContext* VulkanContext::s_Instance = nullptr;

    VulkanContext::VulkanContext(GLFWwindow* windowHandle) : m_WindowHandle(windowHandle)
    {
        ENGINE_CORE_RELEASE_ASSERT(windowHandle, "Window handle is null!");
    }

    VulkanContext::~VulkanContext()
    {
        s_Instance = nullptr;

        if (m_Device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(m_Device);
        }

        // Destroy ImGui resources
        DestroyImGuiResources();

        // Destroy sync objects
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (m_RenderFinishedSemaphores[i] != VK_NULL_HANDLE)
                vkDestroySemaphore(m_Device, m_RenderFinishedSemaphores[i], nullptr);
            if (m_ImageAvailableSemaphores[i] != VK_NULL_HANDLE)
                vkDestroySemaphore(m_Device, m_ImageAvailableSemaphores[i], nullptr);
            if (m_InFlightFences[i] != VK_NULL_HANDLE)
                vkDestroyFence(m_Device, m_InFlightFences[i], nullptr);
        }

        m_CommandBuffers.clear();
        m_Swapchain.reset();

        if (m_Device != VK_NULL_HANDLE)
        {
            vkDestroyDevice(m_Device, nullptr);
        }
        if (m_Surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
        }
        if (m_Instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(m_Instance, nullptr);
        }
    }

    void VulkanContext::Init()
    {
        ENGINE_CORE_RELEASE_ASSERT(s_Instance == nullptr, "VulkanContext already initialized!");
        s_Instance = this;

        CreateInstance();
        CreateSurface();
        SelectPhysicalDevice();
        CreateDevice();
        CreateSwapchain();
        CreateCommandBuffers();
        CreateSyncObjects();
        CreateImGuiResources();

        ENGINE_CORE_INFO("Vulkan Context initialized successfully");
    }

    void VulkanContext::SwapBuffers()
    {
        // Basic present flow (simplified for now, full frame rendering will be implemented later)
        
        // Wait for previous frame
        vkWaitForFences(m_Device, 1, &m_InFlightFences[m_CurrentFrame], VK_TRUE, UINT64_MAX);

        // Acquire next image
        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(m_Device, m_Swapchain->GetSwapchain(), UINT64_MAX,
                                                m_ImageAvailableSemaphores[m_CurrentFrame], VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            // Swapchain needs recreation (window resize)
            ENGINE_CORE_WARN("Swapchain out of date, recreation needed");
            return;
        }
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR,
                                    "Failed to acquire swapchain image!");

        m_CurrentImageIndex = imageIndex; // Store for ImGui rendering
        vkResetFences(m_Device, 1, &m_InFlightFences[m_CurrentFrame]);

        // TODO: Record and submit command buffer here (when render passes are implemented)

        // Present
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores    = &m_RenderFinishedSemaphores[m_CurrentFrame];

        VkSwapchainKHR swapchains[] = {m_Swapchain->GetSwapchain()};
        presentInfo.swapchainCount  = 1;
        presentInfo.pSwapchains     = swapchains;
        presentInfo.pImageIndices   = &imageIndex;

        result = vkQueuePresentKHR(m_GraphicsQueue, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            ENGINE_CORE_WARN("Swapchain suboptimal or out of date at present");
        }
        else
        {
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to present swapchain image!");
        }

        m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    VulkanCommandBuffer* VulkanContext::GetCommandBuffer(uint32_t index) const
    {
        ENGINE_CORE_RELEASE_ASSERT(index < m_CommandBuffers.size(), "Command buffer index out of range!");
        return m_CommandBuffers[index].get();
    }

    void VulkanContext::CreateInstance()
    {
        VkApplicationInfo appInfo  = {};
        appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName   = "Engine";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName        = "Engine";
        appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion         = VK_API_VERSION_1_2;

        // Get GLFW required extensions
        uint32_t     glfwExtensionCount = 0;
        const char** glfwExtensions     = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        VkInstanceCreateInfo createInfo    = {};
        createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo        = &appInfo;
        createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        VkResult result = vkCreateInstance(&createInfo, nullptr, &m_Instance);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan instance!");

        ENGINE_CORE_INFO("Vulkan instance created");
    }

    void VulkanContext::CreateSurface()
    {
        VkResult result = glfwCreateWindowSurface(m_Instance, m_WindowHandle, nullptr, &m_Surface);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create window surface!");

        ENGINE_CORE_INFO("Vulkan surface created");
    }

    void VulkanContext::SelectPhysicalDevice()
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
        ENGINE_CORE_RELEASE_ASSERT(deviceCount > 0, "Failed to find GPUs with Vulkan support!");

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

        // For now, just pick the first device
        // TODO: Add device suitability scoring
        m_PhysicalDevice = devices[0];

        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(m_PhysicalDevice, &properties);
        ENGINE_CORE_INFO("Selected GPU: {0}", properties.deviceName);

        // Find graphics queue family
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; ++i)
        {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                VkBool32 presentSupport = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(m_PhysicalDevice, i, m_Surface, &presentSupport);

                if (presentSupport)
                {
                    m_GraphicsQueueFamily = i;
                    ENGINE_CORE_INFO("Graphics queue family: {0}", i);
                    break;
                }
            }
        }
    }

    void VulkanContext::CreateDevice()
    {
        float                   queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo     = {};
        queueInfo.sType                       = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex            = m_GraphicsQueueFamily;
        queueInfo.queueCount                  = 1;
        queueInfo.pQueuePriorities            = &queuePriority;

        std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        VkPhysicalDeviceFeatures deviceFeatures = {};

        VkDeviceCreateInfo createInfo      = {};
        createInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount    = 1;
        createInfo.pQueueCreateInfos       = &queueInfo;
        createInfo.enabledExtensionCount   = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();
        createInfo.pEnabledFeatures        = &deviceFeatures;

        VkResult result = vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create logical device!");

        vkGetDeviceQueue(m_Device, m_GraphicsQueueFamily, 0, &m_GraphicsQueue);

        ENGINE_CORE_INFO("Vulkan logical device created");
    }

    void VulkanContext::CreateSwapchain()
    {
        int width, height;
        glfwGetFramebufferSize(m_WindowHandle, &width, &height);

        m_Swapchain = std::make_unique<VulkanSwapchain>(m_PhysicalDevice, m_Device, m_Surface,
                                                        static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    }

    void VulkanContext::CreateCommandBuffers()
    {
        uint32_t imageCount = m_Swapchain->GetImageCount();
        m_CommandBuffers.resize(imageCount);
        
        for (uint32_t i = 0; i < imageCount; i++)
        {
            m_CommandBuffers[i] = std::make_unique<VulkanCommandBuffer>(m_Device, m_GraphicsQueueFamily);
        }
        
        ENGINE_CORE_INFO("Created {} command buffers", imageCount);
    }

    void VulkanContext::CreateSyncObjects()
    {
        m_ImageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        m_RenderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        m_InFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VkResult result = vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create image available semaphore!");
            
            result = vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create render finished semaphore!");
            
            result = vkCreateFence(m_Device, &fenceInfo, nullptr, &m_InFlightFences[i]);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create in-flight fence!");
        }
        
        ENGINE_CORE_INFO("Created sync objects for {} frames in flight", MAX_FRAMES_IN_FLIGHT);
    }

    void VulkanContext::CreateImGuiResources()
    {
        // Create RenderPass for ImGui (renders directly to swapchain)
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format         = m_Swapchain->GetImageFormat();
        colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD; // Keep previous content (scene)
        colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &colorRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments    = &colorAttachment;
        renderPassInfo.subpassCount    = 1;
        renderPassInfo.pSubpasses      = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies   = &dependency;

        VkResult result = vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_ImGuiRenderPass);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create ImGui render pass!");

        // Create command pool for ImGui
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = m_GraphicsQueueFamily;

        result = vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_ImGuiCommandPool);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create ImGui command pool!");

        // Create command buffers for ImGui (one per swapchain image)
        uint32_t imageCount = m_Swapchain->GetImageCount();
        m_ImGuiCommandBuffers.resize(imageCount);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = m_ImGuiCommandPool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = imageCount;

        result = vkAllocateCommandBuffers(m_Device, &allocInfo, m_ImGuiCommandBuffers.data());
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to allocate ImGui command buffers!");

        // Create framebuffers for ImGui (one per swapchain image view)
        m_ImGuiFramebuffers.resize(imageCount);
        const auto& imageViews = m_Swapchain->GetImageViews();
        VkExtent2D  extent     = m_Swapchain->GetExtent();

        for (uint32_t i = 0; i < imageCount; i++)
        {
            VkImageView attachments[] = {imageViews[i]};

            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass      = m_ImGuiRenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments    = attachments;
            fbInfo.width           = extent.width;
            fbInfo.height          = extent.height;
            fbInfo.layers          = 1;

            result = vkCreateFramebuffer(m_Device, &fbInfo, nullptr, &m_ImGuiFramebuffers[i]);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create ImGui framebuffer!");
        }

        ENGINE_CORE_INFO("Created ImGui Vulkan resources (RenderPass, CommandPool, {} Framebuffers)", imageCount);
    }

    void VulkanContext::DestroyImGuiResources()
    {
        for (auto fb : m_ImGuiFramebuffers)
        {
            if (fb != VK_NULL_HANDLE)
                vkDestroyFramebuffer(m_Device, fb, nullptr);
        }
        m_ImGuiFramebuffers.clear();

        if (m_ImGuiCommandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(m_Device, m_ImGuiCommandPool, nullptr);
            m_ImGuiCommandPool = VK_NULL_HANDLE;
        }
        m_ImGuiCommandBuffers.clear();

        if (m_ImGuiRenderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(m_Device, m_ImGuiRenderPass, nullptr);
            m_ImGuiRenderPass = VK_NULL_HANDLE;
        }
    }

    void VulkanContext::RenderImGui(void* drawData)
    {
#if defined(ENGINE_ENABLE_VULKAN)
        if (!drawData)
            return;

        ImDrawData* imDrawData = static_cast<ImDrawData*>(drawData);

        VkCommandBuffer cmd = m_ImGuiCommandBuffers[m_CurrentImageIndex];

        // Begin command buffer
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkResetCommandBuffer(cmd, 0);
        vkBeginCommandBuffer(cmd, &beginInfo);

        // Begin render pass
        VkRenderPassBeginInfo rpInfo{};
        rpInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass        = m_ImGuiRenderPass;
        rpInfo.framebuffer       = m_ImGuiFramebuffers[m_CurrentImageIndex];
        rpInfo.renderArea.offset = {0, 0};
        rpInfo.renderArea.extent = m_Swapchain->GetExtent();

        // No clear values since we're loading previous content
        rpInfo.clearValueCount = 0;
        rpInfo.pClearValues    = nullptr;

        vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Record ImGui draw commands
        ImGui_ImplVulkan_RenderDrawData(imDrawData, cmd);

        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);

        // Submit command buffer (will be submitted as part of main frame submission)
        // For now, submit immediately for simplicity
        VkSubmitInfo submitInfo{};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &cmd;

        vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_GraphicsQueue); // Simplified sync for now
#endif
    }

} // namespace Engine
