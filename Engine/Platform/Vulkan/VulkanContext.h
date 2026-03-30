#pragma once

#include "Renderer/GraphicsContext.h"

#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace Engine
{

    class VulkanContext : public GraphicsContext
    {
    public:
        VulkanContext(GLFWwindow* windowHandle);
        ~VulkanContext();

        void Init() override;
        void SwapBuffers() override;

        VkInstance       GetInstance() const { return m_Instance; }
        VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
        VkDevice         GetDevice() const { return m_Device; }
        VkSurfaceKHR     GetSurface() const { return m_Surface; }

    private:
        void CreateInstance();
        void SelectPhysicalDevice();
        void CreateDevice();
        void CreateSurface();

    private:
        GLFWwindow* m_WindowHandle;

        VkInstance       m_Instance       = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice         m_Device         = VK_NULL_HANDLE;
        VkSurfaceKHR     m_Surface        = VK_NULL_HANDLE;

        uint32_t m_GraphicsQueueFamily = 0;
        VkQueue  m_GraphicsQueue       = VK_NULL_HANDLE;
    };

} // namespace Engine
