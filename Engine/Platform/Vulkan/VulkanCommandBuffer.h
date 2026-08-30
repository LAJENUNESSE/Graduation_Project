#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Engine
{

    class VulkanCommandBuffer
    {
    public:
        explicit VulkanCommandBuffer(VkCommandBuffer commandBuffer = VK_NULL_HANDLE);

        void            SetHandle(VkCommandBuffer commandBuffer);
        VkCommandBuffer GetHandle() const { return m_CommandBuffer; }

        void Reset(VkCommandBufferResetFlags flags = 0) const;
        void Begin(VkCommandBufferUsageFlags usageFlags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) const;
        void End() const;

        // ---- Phase 7 compute 命令封装 ----

        void BindComputePipeline(VkPipeline pipeline) const;

        // ---- Phase 8.2 graphics 命令封装 ----

        void BeginRenderPass(VkRenderPass                     renderPass,
                             VkFramebuffer                    framebuffer,
                             VkRect2D                         renderArea,
                             const std::vector<VkClearValue>& clearValues) const;

        void EndRenderPass() const;

        void BindGraphicsPipeline(VkPipeline pipeline) const;

        void SetViewport(float x, float y, float width, float height) const;

        void SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) const;

        void BindVertexBuffers(uint32_t                         firstBinding,
                               const std::vector<VkBuffer>&     buffers,
                               const std::vector<VkDeviceSize>& offsets = {}) const;

        // 项目 IndexBuffer 统一为 uint32 索引（Engine/src/Renderer/Buffer.cpp:143）
        void BindIndexBuffer(VkBuffer buffer, VkDeviceSize offset = 0) const;

        void Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance = 0) const;

        void DrawIndexed(uint32_t indexCount,
                         uint32_t instanceCount = 1,
                         uint32_t firstIndex    = 0,
                         int32_t  vertexOffset  = 0,
                         uint32_t firstInstance = 0) const;

        void BindDescriptorSets(VkPipelineBindPoint                 bindPoint,
                                VkPipelineLayout                    layout,
                                uint32_t                            firstSet,
                                const std::vector<VkDescriptorSet>& sets) const;

        void PushConstants(VkPipelineLayout   layout,
                           VkShaderStageFlags stageFlags,
                           uint32_t           offset,
                           uint32_t           size,
                           const void*        data) const;

        void Dispatch(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1) const;

        // ---- GPU 序 buffer 变更（GL"流内 CPU 写"语义的 Vulkan 等价物）----
        // 粒子 counter 等跨帧累计的 buffer 需要"写入严格处于两次 dispatch 之间"的
        // 顺序；host 立即写与 2 帧在飞的 GPU 工作无顺序保障，必须录制进帧 cmd。

        // vkCmdFillBuffer：以 4 字节对齐的 value 填充区域（offset/size 均为 4 的倍数）
        void FillBuffer(VkBuffer dst, VkDeviceSize offset, VkDeviceSize size, uint32_t value) const;

        // vkCmdUpdateBuffer：≤65536B 内嵌数据更新（数据在调用时即拷入 cmd buffer）
        void UpdateBuffer(VkBuffer dst, VkDeviceSize offset, VkDeviceSize size, const void* data) const;

        // 单 buffer barrier
        void BufferBarrier(VkBuffer             buffer,
                           VkDeviceSize         offset,
                           VkDeviceSize         size,
                           VkPipelineStageFlags srcStage,
                           VkPipelineStageFlags dstStage,
                           VkAccessFlags        srcAccess,
                           VkAccessFlags        dstAccess) const;

        // 全局内存 barrier（不需指定 buffer/image，覆盖所有资源）
        void MemoryBarrier(VkPipelineStageFlags srcStage,
                           VkPipelineStageFlags dstStage,
                           VkAccessFlags        srcAccess,
                           VkAccessFlags        dstAccess) const;

        // 单 image layout transition + barrier
        void ImageBarrier(VkImage              image,
                          VkImageLayout        oldLayout,
                          VkImageLayout        newLayout,
                          VkPipelineStageFlags srcStage,
                          VkPipelineStageFlags dstStage,
                          VkAccessFlags        srcAccess,
                          VkAccessFlags        dstAccess,
                          VkImageAspectFlags   aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                          uint32_t             mipLevels  = 1,
                          uint32_t             layers     = 1) const;

    private:
        VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
    };

} // namespace Engine
