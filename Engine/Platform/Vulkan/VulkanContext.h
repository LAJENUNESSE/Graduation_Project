#pragma once

#include "Renderer/GraphicsContext.h"

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

struct GLFWwindow;

namespace Engine
{
    class VulkanSwapchain;
    class VulkanCommandBuffer;

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
        VkQueue          GetGraphicsQueue() const { return m_GraphicsQueue; }
        uint32_t         GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }

        VulkanSwapchain*     GetSwapchain() const { return m_Swapchain.get(); }
        VulkanCommandBuffer* GetCommandBuffer(uint32_t index) const;

    private:
        void CreateInstance();
        void SelectPhysicalDevice();
        void CreateDevice();
        void CreateSurface();
        void CreateSwapchain();
        void CreateCommandBuffers();
        void CreateSyncObjects();

    private:
        GLFWwindow* m_WindowHandle;

        VkInstance       m_Instance       = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice         m_Device         = VK_NULL_HANDLE;
        VkSurfaceKHR     m_Surface        = VK_NULL_HANDLE;

        uint32_t m_GraphicsQueueFamily = 0;
        VkQueue  m_GraphicsQueue       = VK_NULL_HANDLE;

        std::unique_ptr<VulkanSwapchain>                   m_Swapchain;
        std::vector<std::unique_ptr<VulkanCommandBuffer>> m_CommandBuffers;

        // Sync objects (per frame in flight)
        static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
        std::vector<VkSemaphore>  m_ImageAvailableSemaphores;
        std::vector<VkSemaphore>  m_RenderFinishedSemaphores;
        std::vector<VkFence>      m_InFlightFences;
        uint32_t                  m_CurrentFrame = 0;
    };

} // namespace Engine
