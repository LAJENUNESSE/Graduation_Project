#include "engpch.h"
#include "Platform/Vulkan/VulkanSceneState.h"

#include "Core/Log.h"

namespace Engine
{

    const VulkanSceneState::TextureBinding VulkanSceneState::kInvalidBinding{};

    void VulkanSceneState::BindTextureSlot(uint32_t slot, VkImageView view, VkSampler sampler, VkImageLayout layout)
    {
        if (slot >= kMaxTextureSlots)
        {
            static bool warnedRange = false;
            if (!warnedRange)
            {
                warnedRange = true;
                ENGINE_CORE_WARN("[Vulkan] Texture slot {0} out of range (max {1})", slot, kMaxTextureSlots - 1);
            }
            return;
        }

        auto& binding   = m_TextureSlots[slot];
        binding.View    = view;
        binding.Sampler = sampler;
        binding.Layout  = layout;
        binding.Valid   = (view != VK_NULL_HANDLE);
    }

    void VulkanSceneState::UnbindTextureSlot(uint32_t slot)
    {
        if (slot < kMaxTextureSlots)
            m_TextureSlots[slot] = {};
    }

    void VulkanSceneState::BindStorageSlot(uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range)
    {
        if (binding >= kMaxStorageSlots)
        {
            static bool warnedRange = false;
            if (!warnedRange)
            {
                warnedRange = true;
                ENGINE_CORE_WARN("[Vulkan] Storage slot {0} out of range (max {1})", binding, kMaxStorageSlots - 1);
            }
            return;
        }

        auto& bindingData  = m_StorageSlots[binding];
        bindingData.Buffer = buffer;
        bindingData.Offset = offset;
        bindingData.Range  = range;
        bindingData.Valid  = (buffer != VK_NULL_HANDLE);
    }

    void VulkanSceneState::ResetTextureSlots()
    {
        m_TextureSlots.fill({});
    }

    const VulkanSceneState::TextureBinding& VulkanSceneState::GetTextureSlot(uint32_t slot) const
    {
        return (slot < kMaxTextureSlots) ? m_TextureSlots[slot] : kInvalidBinding;
    }

    const VulkanSceneState::StorageBinding& VulkanSceneState::GetStorageSlot(uint32_t binding) const
    {
        static const StorageBinding kInvalidStorage{};
        return (binding < kMaxStorageSlots) ? m_StorageSlots[binding] : kInvalidStorage;
    }

} // namespace Engine
