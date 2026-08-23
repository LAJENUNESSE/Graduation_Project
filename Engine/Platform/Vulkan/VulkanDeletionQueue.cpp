#include "engpch.h"
#include "Platform/Vulkan/VulkanDeletionQueue.h"

#include "Core/Assert.h"

namespace Engine
{

    void VulkanDeletionQueue::Submit(uint32_t slot, std::function<void(VkDevice)>&& fn)
    {
        ENGINE_CORE_RELEASE_ASSERT(slot < kSlotCount, "Deletion queue slot out of range");
        if (fn)
            m_Slots[slot].push_back(std::move(fn));
    }

    void VulkanDeletionQueue::FlushSlot(uint32_t slot, VkDevice device)
    {
        ENGINE_CORE_RELEASE_ASSERT(slot < kSlotCount, "Deletion queue slot out of range");

        auto& callbacks = m_Slots[slot];
        for (auto& callback : callbacks)
        {
            if (callback)
                callback(device);
        }
        callbacks.clear();
    }

    void VulkanDeletionQueue::FlushAll(VkDevice device)
    {
        for (uint32_t slot = 0; slot < kSlotCount; ++slot)
            FlushSlot(slot, device);
    }

} // namespace Engine
