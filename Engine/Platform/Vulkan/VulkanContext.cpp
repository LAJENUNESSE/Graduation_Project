#include "engpch.h"
#include "Platform/Vulkan/VulkanContext.h"

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

    VulkanContext::VulkanContext(GLFWwindow* windowHandle) : m_WindowHandle(windowHandle)
    {
        ENGINE_CORE_RELEASE_ASSERT(windowHandle, "Window handle is null!");
    }

    VulkanContext::~VulkanContext()
    {
        if (m_Device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(m_Device);
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
        CreateInstance();
        CreateSurface();
        SelectPhysicalDevice();
        CreateDevice();

        ENGINE_CORE_INFO("Vulkan Context initialized successfully");
    }

    void VulkanContext::SwapBuffers()
    {
        // Swapchain present will be handled by VulkanSwapchain
        // This is a placeholder for now
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

} // namespace Engine
