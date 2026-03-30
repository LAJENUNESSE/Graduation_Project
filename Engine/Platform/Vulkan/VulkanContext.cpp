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

} // namespace Engine
