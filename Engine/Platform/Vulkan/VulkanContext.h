#pragma once

#include "Renderer/GraphicsContext.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

struct GLFWwindow;
struct ImDrawData;

namespace Engine
{

    class VulkanContext : public GraphicsContext
    {
    public:
        explicit VulkanContext(GLFWwindow* windowHandle);
        ~VulkanContext() override;

        void Init() override;
        void SwapBuffers() override;

        void QueueDrawArrays(uint32_t count, uint32_t firstVertex = 0);
        void QueueDrawArraysInstanced(uint32_t count, uint32_t instanceCount, uint32_t firstVertex = 0);
        void QueueDrawIndexed(uint32_t indexCount, uint32_t firstIndex = 0, int32_t vertexOffset = 0);
        void QueueDrawLines(uint32_t count, uint32_t firstVertex = 0);

        void SetClearColor(const glm::vec4& color) { m_ClearColor = color; }
        void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
        {
            m_ViewportX      = x;
            m_ViewportY      = y;
            m_ViewportWidth  = width;
            m_ViewportHeight = height;
        }

        // Singleton accessor (set during Init, cleared on destruction)
        static VulkanContext* Get() { return s_Instance; }

        // Accessors for ImGui Vulkan integration
        VkInstance       GetInstance() const { return m_Instance; }
        VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
        VkDevice         GetDevice() const { return m_Device; }
        uint32_t         GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }
        VkQueue          GetGraphicsQueue() const { return m_GraphicsQueue; }
        uint32_t         GetComputeQueueFamily() const { return m_ComputeQueueFamily; }
        VkQueue          GetComputeQueue() const { return m_ComputeQueue; }
        bool             HasDedicatedComputeQueue() const { return m_ComputeQueueFamily != m_GraphicsQueueFamily; }
        VkRenderPass     GetImGuiRenderPass() const { return m_ImGuiRenderPass; }

        // Forward-declared; swapchain info for ImGui
        struct SwapchainInfo
        {
            uint32_t GetImageCount() const { return ImageCount; }
            uint32_t ImageCount = 0;
        };
        const SwapchainInfo* GetSwapchain() const { return &m_SwapchainInfo; }

        void RenderImGui(void* drawData);

        VkCommandBuffer BeginSingleTimeCommands();
        void            EndSingleTimeCommands(VkCommandBuffer commandBuffer);

        VkCommandPool GetCommandPool() const { return m_CommandPool; }

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
        void CreateImGuiRenderPass();
        void CreateImGuiFramebuffers();
        void DestroyImGuiFramebuffers();
        void RecordImGuiPass(VkCommandBuffer cmd, uint32_t imageIndex);
        void CreateDebugDrawResources();
        void DestroyDebugDrawResources();

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
        VkQueue  m_GraphicsQueue       = VK_NULL_HANDLE;
        VkQueue  m_PresentQueue        = VK_NULL_HANDLE;
        VkQueue  m_ComputeQueue        = VK_NULL_HANDLE;
        uint32_t m_GraphicsQueueFamily = 0;
        uint32_t m_PresentQueueFamily  = 0;
        uint32_t m_ComputeQueueFamily  = 0;

        // Swapchain
        VkSwapchainKHR           m_Swapchain = VK_NULL_HANDLE;
        VkFormat                 m_SwapchainFormat;
        VkExtent2D               m_SwapchainExtent;
        std::vector<VkImage>     m_SwapchainImages;
        std::vector<VkImageView> m_SwapchainImageViews;
        glm::vec4                m_ClearColor     = {0.392f, 0.584f, 0.929f, 1.0f};
        uint32_t                 m_ViewportX      = 0;
        uint32_t                 m_ViewportY      = 0;
        uint32_t                 m_ViewportWidth  = 0;
        uint32_t                 m_ViewportHeight = 0;

        enum class DebugPrimitiveType : uint8_t
        {
            Triangles = 0,
            Lines     = 1
        };

        struct PendingDrawCall
        {
            uint32_t           VertexCount   = 0;
            uint32_t           FirstVertex   = 0;
            uint32_t           InstanceCount = 1;
            DebugPrimitiveType Primitive     = DebugPrimitiveType::Triangles;
        };
        std::vector<PendingDrawCall> m_PendingDrawCalls;

        VkRenderPass               m_DebugRenderPass     = VK_NULL_HANDLE;
        VkPipelineLayout           m_DebugPipelineLayout = VK_NULL_HANDLE;
        VkPipeline                 m_DebugPipeline       = VK_NULL_HANDLE;
        VkPipeline                 m_DebugLinePipeline   = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> m_DebugFramebuffers;

        // Per-frame command resources
        VkCommandPool                m_CommandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> m_CommandBuffers;

        // Synchronization (one set per MAX_FRAMES_IN_FLIGHT)
        static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
        std::vector<VkSemaphore>  m_ImageAvailableSemaphores;
        std::vector<VkSemaphore>  m_RenderFinishedSemaphores;
        std::vector<VkFence>      m_InFlightFences;
        uint32_t                  m_CurrentFrame = 0;

        bool m_FramebufferResized = false;

        // ImGui render pass (created lazily)
        VkRenderPass               m_ImGuiRenderPass = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> m_ImGuiFramebuffers;
        ImDrawData*                m_PendingImGuiDrawData = nullptr;
        SwapchainInfo              m_SwapchainInfo;

        static VulkanContext* s_Instance;

#ifdef NDEBUG
        static constexpr bool s_EnableValidation = false;
#else
        static constexpr bool s_EnableValidation = true;
#endif
    };

} // namespace Engine
