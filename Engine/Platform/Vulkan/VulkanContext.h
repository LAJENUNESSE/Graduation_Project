#pragma once

#include "Platform/Vulkan/VulkanDeletionQueue.h"
#include "Platform/Vulkan/VulkanGraphicsPipelineBuilder.h"
#include "Platform/Vulkan/VulkanSceneDrawDispatcher.h"
#include "Platform/Vulkan/VulkanSceneState.h"
#include "Renderer/GraphicsContext.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <unordered_map>
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

        // 高级帧录制接口（GraphicsContext override）— 主循环显式驱动帧边界。
        // BeginRenderFrame 等价 BeginFrame；EndRenderFrame 录默认 pass（清屏/debug/ImGui）
        // + EndFrame。SwapBuffers 在 Vulkan path 下退化为 no-op（帧已由主循环结束）。
        void BeginRenderFrame() override;
        void EndRenderFrame() override;

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

        // 把 GPU 对象销毁回调推迟到当前帧槽位的下一轮 fence 确认之后执行。
        // 录制窗口内（BeginFrame ~ EndFrame）禁止同步 vkDestroy*——vkDeviceWaitIdle
        // 保护不了尚未提交的命令缓冲；场景切换等运行期资源释放一律走这里。
        // context 不存在（已析构）时丢弃回调，与资源类 Destroy 的现状语义一致。
        static void DeferDestroy(std::function<void(VkDevice)>&& fn);

        // 在 ImGui 后端关闭前冲刷已完成 GPU 工作对应的延迟销毁回调。
        // 视口/场景销毁可能在录制帧内排队 ImGui descriptor 的释放，必须在
        // ImGui_ImplVulkan_Shutdown 之前执行这些回调。
        void FlushDeferredDestructions();

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

        // ---------------------------------------------------------------------
        // 高级帧录制接口（Phase 7+）
        //
        // SwapBuffers() 是 BeginFrame() + 内部清屏/Debug/ImGui Pass + EndFrame() 的
        // 复合包装；高级调用方（compute pass / Scene 渲染）可以直接：
        //   if (ctx->BeginFrame()) {
        //       VkCommandBuffer cmd = ctx->GetCurrentFrameCommandBuffer();
        //       ... 录制 dispatch / 渲染 Pass ...
        //       ctx->RecordImGuiPass(cmd, ctx->GetCurrentImageIndex());  // 可选
        //       ctx->EndFrame();
        //   }
        // 绕过 SwapBuffers 的清屏与默认 ImGui 录制。SmokeLayer 仍走 SwapBuffers。
        // ---------------------------------------------------------------------
        bool            BeginFrame();
        void            EndFrame();
        VkCommandBuffer GetCurrentFrameCommandBuffer() const
        {
            return m_FrameInProgress ? m_CommandBuffers[m_CurrentFrame] : VK_NULL_HANDLE;
        }
        uint32_t GetCurrentImageIndex() const { return m_PendingImageIndex; }
        uint32_t GetCurrentFrameIndex() const { return m_CurrentFrame; }

        // 注册需在本帧主 submit 完成后信号化的 fence（追加零 cmd submit）。
        // VulkanAsyncReadback 用它感知 host-visible staging buffer 何时可读。
        void RegisterReadbackFenceSignal(VkFence fence);

        // ImGui pass 录制：BeginFrame/EndFrame 高级用法可在自定义渲染后调用。
        // 仍依赖 RenderImGui(drawData) 缓存的 drawData。
        void RecordImGuiPass(VkCommandBuffer cmd, uint32_t imageIndex);

        // Phase 8.2 场景渲染状态机（当前 shader + 纹理槽），由
        // VulkanShader/Texture::Bind 写入、DrawIndexed/DrawArrays 录制时消费。
        VulkanSceneState& GetSceneState() { return m_SceneState; }

        // Phase 8.2 scene graphics pipeline 组装与缓存
        VulkanGraphicsPipelineBuilder& GetPipelineBuilder() { return m_PipelineBuilder; }

        // Phase 8.2 场景绘制分发器（UBO 打包 / descriptor / 录制）
        VulkanSceneDrawDispatcher& GetSceneDrawDispatcher() { return m_SceneDrawDispatcher; }

        // 当前激活的场景 renderpass（Framebuffer::Bind 录制 BeginRenderPass 时写入；
        // Unbind 清空）。VK_NULL_HANDLE 表示不在场景 pass 内，绘制走 debug fallback。
        // 尺寸/hasDepth 供 Clear() 的 vkCmdClearAttachments 与 draw 前防御性
        // viewport/scissor 重录使用。
        void SetActiveSceneRenderPass(
            VkRenderPass pass, uint32_t colorAttachmentCount, bool hasDepth, uint32_t width, uint32_t height)
        {
            m_ActiveSceneRenderPass           = pass;
            m_ActiveSceneColorAttachmentCount = colorAttachmentCount;
            m_ActiveSceneHasDepth             = hasDepth;
            m_ActiveSceneWidth                = width;
            m_ActiveSceneHeight               = height;
        }
        VkRenderPass     GetActiveSceneRenderPass() const { return m_ActiveSceneRenderPass; }
        const glm::vec4& GetClearColor() const { return m_ClearColor; }
        VkSampler        GetDefaultSampler() const { return m_DefaultSampler; }

        // Phase 8.2：把 FBO color attachment 的 VkImageView 包装成 ImGui 可用的
        // ImTextureID（VkDescriptorSet，void* 透传）。带 view→set 缓存；
        // view 销毁前必须调 RemoveImGuiTexture 防止悬垂。
        void* GetImGuiTextureForView(void* imageView);
        void  RemoveImGuiTexture(void* imageView);
        // 退出路径专用：在 ImGui_ImplVulkan_Shutdown 前释放全部 ImGui descriptor
        // set 并清空缓存，此后析构 FBO 触发的 RemoveImGuiTexture 只清缓存不再 free。
        void     ClearImGuiTextures();
        uint32_t GetActiveSceneColorAttachmentCount() const { return m_ActiveSceneColorAttachmentCount; }
        bool     GetActiveSceneHasDepth() const { return m_ActiveSceneHasDepth; }
        uint32_t GetActiveSceneWidth() const { return m_ActiveSceneWidth; }
        uint32_t GetActiveSceneHeight() const { return m_ActiveSceneHeight; }

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
        void CreateImageSemaphores();
        void DestroyImageSemaphores();
        void CreateImGuiRenderPass();
        void CreateImGuiFramebuffers();
        void DestroyImGuiFramebuffers();
        void CreateDebugDrawResources();
        void CreateDefaultSampler();
        void DestroyDebugDrawResources();

        // SwapBuffers 内"BeginFrame 之后到 EndFrame 之前"那段（清屏 / debug draw / ImGui pass）
        // 抽出供 EndRenderFrame 复用。cmd / imageIndex 来自当前帧。
        void RecordDefaultFramePasses(VkCommandBuffer cmd, uint32_t imageIndex);

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

        // Synchronization（结构说明见 VulkanSynchronization.h 注释）
        static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
        VkFence                   m_AcquireFence       = VK_NULL_HANDLE;
        std::vector<VkSemaphore>  m_RenderFinishedSemaphores; // 大小 = swapchain image count
        std::vector<VkFence>      m_InFlightFences;
        uint32_t                  m_CurrentFrame = 0;

        static_assert(VulkanDeletionQueue::kSlotCount == 2, "deletion queue slots must match MAX_FRAMES_IN_FLIGHT");

        // GPU 资源延迟删除（见 VulkanDeletionQueue 注释）
        VulkanDeletionQueue m_DeletionQueue;

        // 高级帧录制状态（BeginFrame/EndFrame）
        bool                 m_FrameInProgress   = false;
        uint32_t             m_PendingImageIndex = 0;
        std::vector<VkFence> m_PendingReadbackFences; // EndFrame 末尾零 cmd submit 信号化

        bool m_FramebufferResized = false;

        // ImGui render pass (created lazily)
        VkRenderPass               m_ImGuiRenderPass = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> m_ImGuiFramebuffers;
        ImDrawData*                m_PendingImGuiDrawData = nullptr;
        SwapchainInfo              m_SwapchainInfo;

        VulkanSceneState                           m_SceneState;
        VulkanGraphicsPipelineBuilder              m_PipelineBuilder;
        VkSampler                                  m_DefaultSampler = VK_NULL_HANDLE;
        std::unordered_map<void*, VkDescriptorSet> m_ImGuiTextures;
        VulkanSceneDrawDispatcher                  m_SceneDrawDispatcher;

        VkRenderPass m_ActiveSceneRenderPass           = VK_NULL_HANDLE;
        uint32_t     m_ActiveSceneColorAttachmentCount = 0;
        bool         m_ActiveSceneHasDepth             = false;
        uint32_t     m_ActiveSceneWidth                = 0;
        uint32_t     m_ActiveSceneHeight               = 0;

        static VulkanContext* s_Instance;

// TODO(phase-8.2): 临时强制开启 validation 排查 device lost，确认修复后还原为
// 按 NDEBUG 门控（RelWithDebInfo 下关闭）。
#ifdef NDEBUG
        static constexpr bool s_EnableValidation = true;
#else
        static constexpr bool s_EnableValidation = true;
#endif
    };

} // namespace Engine
