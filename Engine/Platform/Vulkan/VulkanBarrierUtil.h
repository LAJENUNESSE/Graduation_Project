#pragma once

#include "Renderer/RendererAPI.h"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace Engine
{

    // OpenGL barrier bit → Vulkan pipeline stage / access mask 四元组
    // 用于 Phase 7 compute 迁移：把 RenderCommand::MemoryBarrier(bits) 转换为
    // VulkanCommandBuffer::MemoryBarrier(srcStage, dstStage, srcAccess, dstAccess)。
    struct VulkanBarrierMasks
    {
        VkPipelineStageFlags SrcStage  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        VkPipelineStageFlags DstStage  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        VkAccessFlags        SrcAccess = VK_ACCESS_SHADER_WRITE_BIT;
        VkAccessFlags        DstAccess = VK_ACCESS_SHADER_READ_BIT;
    };

    // 将 BarrierBit::ShaderStorage | Command | BufferUpdate | All 组合解析为 Vulkan 四元组。
    // 多 bit 组合时 stage/access 取并集（保守同步）。
    VulkanBarrierMasks ResolveBarrierBits(uint32_t bits);

} // namespace Engine
