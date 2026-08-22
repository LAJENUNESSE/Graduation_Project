#include "engpch.h"
#include "Platform/Vulkan/VulkanAsyncReadback.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Debug/GpuMemoryStats.h"
#include "Platform/Vulkan/VulkanAllocator.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanContext.h"

#include <cstring>
#include <vma/vk_mem_alloc.h>

namespace Engine
{

    VulkanAsyncReadback::VulkanAsyncReadback(uint32_t size) : m_Size(size)
    {
        ENGINE_CORE_RELEASE_ASSERT(size > 0, "VulkanAsyncReadback size must be greater than zero");
        ENGINE_CORE_RELEASE_ASSERT(VulkanContext::Get() != nullptr, "VulkanAsyncReadback requires VulkanContext");

        VkDevice device = VulkanContext::Get()->GetDevice();

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size        = size;
        bufferInfo.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = 0; // 创建为 unsignaled，Pending 标志独立追踪槽位状态

        for (Slot& slot : m_Slots)
        {
            VmaAllocationInfo info{};
            VkResult br = vmaCreateBuffer(VulkanAllocator::GetAllocator(), &bufferInfo, &allocInfo, &slot.Staging,
                                          &slot.Alloc, &info);
            ENGINE_CORE_RELEASE_ASSERT(br == VK_SUCCESS, "Failed to create VulkanAsyncReadback staging buffer");
            slot.Mapped = info.pMappedData;
            ENGINE_CORE_RELEASE_ASSERT(slot.Mapped != nullptr,
                                       "VulkanAsyncReadback staging buffer not persistently mapped");

            VkResult fr = vkCreateFence(device, &fenceInfo, nullptr, &slot.Fence);
            ENGINE_CORE_RELEASE_ASSERT(fr == VK_SUCCESS, "Failed to create VulkanAsyncReadback fence");
        }
    }

    VulkanAsyncReadback::~VulkanAsyncReadback()
    {
        VulkanContext* ctx = VulkanContext::Get();
        if (!ctx || !VulkanAllocator::IsInitialized())
            return;

        VkDevice device = ctx->GetDevice();

        // Wait for any pending readback before destroying resources
        for (Slot& slot : m_Slots)
        {
            if (slot.Pending)
                vkWaitForFences(device, 1, &slot.Fence, VK_TRUE, UINT64_MAX);

            if (slot.Fence != VK_NULL_HANDLE)
                vkDestroyFence(device, slot.Fence, nullptr);

            if (slot.Staging != VK_NULL_HANDLE)
                vmaDestroyBuffer(VulkanAllocator::GetAllocator(), slot.Staging, slot.Alloc);
        }
    }

    void VulkanAsyncReadback::CopyFrom(const Ref<ShaderStorageBuffer>& src, uint32_t size, uint32_t srcOffset)
    {
        ENGINE_CORE_RELEASE_ASSERT(size <= m_Size, "VulkanAsyncReadback::CopyFrom size exceeds allocation");
        GpuMemoryStats::Get().AddDownloaded(size);

        VulkanContext* ctx = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(ctx != nullptr, "VulkanAsyncReadback::CopyFrom requires VulkanContext");

        VkCommandBuffer cmd = ctx->GetCurrentFrameCommandBuffer();
        ENGINE_CORE_RELEASE_ASSERT(cmd != VK_NULL_HANDLE,
                                   "VulkanAsyncReadback::CopyFrom must be called inside BeginFrame/EndFrame");

        auto* vkSrc = dynamic_cast<VulkanStorageBuffer*>(src.get());
        ENGINE_CORE_RELEASE_ASSERT(vkSrc != nullptr, "VulkanAsyncReadback::CopyFrom expects VulkanStorageBuffer");

        Slot& slot = m_Slots[m_WriteSlot];

        // 槽内若仍有未消费的 pending readback（ring 满），先等其完成再覆盖，避免数据竞争
        if (slot.Pending)
        {
            VkDevice device = ctx->GetDevice();
            vkWaitForFences(device, 1, &slot.Fence, VK_TRUE, UINT64_MAX);
            vkResetFences(device, 1, &slot.Fence);
            slot.Pending = false;
        }

        VkBufferCopy copy{};
        copy.srcOffset = srcOffset;
        copy.dstOffset = 0;
        copy.size      = size;
        vkCmdCopyBuffer(cmd, vkSrc->GetBuffer(), slot.Staging, 1, &copy);

        slot.BytesUsed = size;
        slot.Pending   = true;

        ctx->RegisterReadbackFenceSignal(slot.Fence);

        m_WriteSlot = (m_WriteSlot + 1) % m_Slots.size();
    }

    bool VulkanAsyncReadback::IsReady() const
    {
        const Slot& slot = m_Slots[m_ReadSlot];
        if (!slot.Pending)
            return false;

        VkDevice device = VulkanContext::Get()->GetDevice();
        VkResult result = vkGetFenceStatus(device, slot.Fence);
        return result == VK_SUCCESS;
    }

    void VulkanAsyncReadback::GetData(void* dest, uint32_t size)
    {
        Slot& slot = m_Slots[m_ReadSlot];
        ENGINE_CORE_RELEASE_ASSERT(slot.Pending,
                                   "VulkanAsyncReadback::GetData called without pending data in current slot");
        ENGINE_CORE_RELEASE_ASSERT(size <= slot.BytesUsed,
                                   "VulkanAsyncReadback::GetData requested size exceeds copy size");

        std::memcpy(dest, slot.Mapped, size);

        VkDevice device = VulkanContext::Get()->GetDevice();
        vkResetFences(device, 1, &slot.Fence);

        slot.Pending = false;
        m_ReadSlot   = (m_ReadSlot + 1) % m_Slots.size();
    }

    void VulkanAsyncReadback::Reset()
    {
        VulkanContext* ctx = VulkanContext::Get();
        if (!ctx)
            return;
        VkDevice device = ctx->GetDevice();

        for (Slot& slot : m_Slots)
        {
            if (slot.Pending)
            {
                vkWaitForFences(device, 1, &slot.Fence, VK_TRUE, UINT64_MAX);
                vkResetFences(device, 1, &slot.Fence);
            }
            slot.Pending = false;
        }
        m_WriteSlot = 0;
        m_ReadSlot  = 0;
    }

    bool VulkanAsyncReadback::IsPending() const
    {
        for (const Slot& slot : m_Slots)
        {
            if (slot.Pending)
                return true;
        }
        return false;
    }

} // namespace Engine
