#include "engpch.h"
#include "Platform/Vulkan/VulkanSceneState.h"

#include "Core/Log.h"

namespace Engine
{

    const VulkanSceneState::TextureBinding VulkanSceneState::kInvalidBinding{};

    void VulkanSceneState::BindTextureSlot(uint32_t slot, VkImageView view, VkSampler sampler)
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
        binding.Valid   = (view != VK_NULL_HANDLE);
    }

    void VulkanSceneState::UnbindTextureSlot(uint32_t slot)
    {
        if (slot < kMaxTextureSlots)
            m_TextureSlots[slot] = {};
    }

    void VulkanSceneState::ResetTextureSlots()
    {
        m_TextureSlots.fill({});
    }

    const VulkanSceneState::TextureBinding& VulkanSceneState::GetTextureSlot(uint32_t slot) const
    {
        return (slot < kMaxTextureSlots) ? m_TextureSlots[slot] : kInvalidBinding;
    }

} // namespace Engine
