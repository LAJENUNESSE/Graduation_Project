#pragma once

#include "Renderer/GraphicsContext.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

struct GLFWwindow;

namespace Engine
{

    class VulkanContext : public GraphicsContext
    {
    public:
        explicit VulkanContext(GLFWwindow* windowHandle);
        ~VulkanContext() override;

        void Init() override;
        void SwapBuffers() override;

        // Singleton accessor (set during Init, cleared on destruction)
        static VulkanContext* Get() { return s_Instance; }

        // Accessors for ImGui Vulkan integration
        VkInstance       GetInstance() const { return m_Instance; }
        VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
        VkDevice         GetDevice() const { return m_Device; }
        uint32_t         GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }
        VkQueue          GetGraphicsQueue() const { return m_GraphicsQueue; }
        VkRenderPass     GetImGuiRenderPass() const { return m_ImGuiRenderPass; }

        // Forward-declared; swapchain info for ImGui
        struct SwapchainInfo
        {
            uint32_t GetImageCount() const { return ImageCount; }
            uint32_t ImageCount = 0;
        };
        const SwapchainInfo* GetSwapchain() const { return &m_SwapchainInfo; }

        void RenderImGui(void* drawData);

    private:
        // Setup helpers (called from Init)
        void CreateInstance();
        void SetupDebugMessenger();
        void CreateSurface();
        void PickPhysicalDevice();
        void CreateLogicalDevice();
        void CreateSwapchain();
        void CreateCommandPool();
        void CreateSyncObjects();

        // Swapchain recreation
        void CleanupSwapchain();
        void RecreateSwapchain();

        // Cleanup
        void Cleanup();

        GLFWwindow* m_WindowHandle = nullptr;

        // Core Vulkan objects
        VkInstance               m_Instance       = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
        VkSurfaceKHR             m_Surface        = VK_NULL_HANDLE;
        VkPhysicalDevice         m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice                 m_Device         = VK_NULL_HANDLE;

        // Queues
        VkQueue  m_GraphicsQueue      = VK_NULL_HANDLE;
        VkQueue  m_PresentQueue       = VK_NULL_HANDLE;
        uint32_t m_GraphicsQueueFamily = 0;
        uint32_t m_PresentQueueFamily  = 0;

        // Swapchain
        VkSwapchainKHR       m_Swapchain = VK_NULL_HANDLE;
        VkFormat             m_SwapchainFormat;
        VkExtent2D           m_SwapchainExtent;
        std::vector<VkImage> m_SwapchainImages;

        // Per-frame command resources
        VkCommandPool              m_CommandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> m_CommandBuffers;

        // Synchronization (one set per MAX_FRAMES_IN_FLIGHT)
        static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
        std::vector<VkSemaphore>  m_ImageAvailableSemaphores;
        std::vector<VkSemaphore>  m_RenderFinishedSemaphores;
        std::vector<VkFence>      m_InFlightFences;
        uint32_t                  m_CurrentFrame = 0;

        bool m_FramebufferResized = false;

        // ImGui render pass (created lazily)
        VkRenderPass m_ImGuiRenderPass = VK_NULL_HANDLE;
        SwapchainInfo m_SwapchainInfo;

        static VulkanContext* s_Instance;

#ifdef NDEBUG
        static constexpr bool s_EnableValidation = false;
#else
        static constexpr bool s_EnableValidation = true;
#endif
    };

} // namespace Engine
