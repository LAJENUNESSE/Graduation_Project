#include "engpch.h"
#include "Platform/Vulkan/VulkanBarrierUtil.h"

namespace Engine
{

    VulkanBarrierMasks ResolveBarrierBits(uint32_t bits)
    {
        // 全 bit：保守取 ALL_COMMANDS + MEMORY_READ|WRITE
        if (bits == BarrierBit::All)
        {
            return VulkanBarrierMasks{
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                VK_ACCESS_MEMORY_WRITE_BIT,
                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            };
        }

        VulkanBarrierMasks m{};
        m.SrcStage  = 0;
        m.DstStage  = 0;
        m.SrcAccess = 0;
        m.DstAccess = 0;

        // ShaderStorage：compute write → compute/vertex/fragment read|write（SSBO 跨 stage 可见）
        if (bits & BarrierBit::ShaderStorage)
        {
            m.SrcStage |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            m.DstStage |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            m.SrcAccess |= VK_ACCESS_SHADER_WRITE_BIT;
            m.DstAccess |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        }

        // Command：compute write → draw indirect read（render_args 写完后被 DrawArraysIndirect 读取）
        if (bits & BarrierBit::Command)
        {
            m.SrcStage |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            m.DstStage |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
            m.SrcAccess |= VK_ACCESS_SHADER_WRITE_BIT;
            m.DstAccess |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        }

        // BufferUpdate：transfer write → compute read（CPU 上传后被 compute 消费）
        if (bits & BarrierBit::BufferUpdate)
        {
            m.SrcStage |= VK_PIPELINE_STAGE_TRANSFER_BIT;
            m.DstStage |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            m.SrcAccess |= VK_ACCESS_TRANSFER_WRITE_BIT;
            m.DstAccess |= VK_ACCESS_SHADER_READ_BIT;
        }

        // 无任何 bit 匹配 → 退化为保守的 compute→compute（避免传 0 给 vkCmdPipelineBarrier）
        if (m.SrcStage == 0)
            m.SrcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        if (m.DstStage == 0)
            m.DstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

        return m;
    }

} // namespace Engine
