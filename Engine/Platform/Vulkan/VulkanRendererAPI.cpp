#include "engpch.h"
#include "Platform/Vulkan/VulkanRendererAPI.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"

#include "Core/Log.h"

#include <vulkan/vulkan.h>

namespace Engine
{

    void VulkanRendererAPI::Init()
    {
        ENGINE_CORE_INFO("VulkanRendererAPI::Init()");

        // Vulkan state is configured through pipelines, not global state like OpenGL.
        // Default state is configured when creating graphics pipelines.

        // Store default state
        m_DepthTestEnabled = true;
        m_DepthFunc        = DepthFunc::Less;
        m_CullFaceEnabled  = true;
        m_CullFaceMode     = CullFaceMode::Back;
        m_BlendEnabled     = true;
        m_SrcBlendFactor   = BlendFactor::SrcAlpha;
        m_DstBlendFactor   = BlendFactor::OneMinusSrcAlpha;
    }

    void VulkanRendererAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        // Cache viewport for command buffer recording
        m_Viewport.x        = static_cast<float>(x);
        m_Viewport.y        = static_cast<float>(y);
        m_Viewport.width    = static_cast<float>(width);
        m_Viewport.height   = static_cast<float>(height);
        m_Viewport.minDepth = 0.0f;
        m_Viewport.maxDepth = 1.0f;

        m_Scissor.offset = {static_cast<int32_t>(x), static_cast<int32_t>(y)};
        m_Scissor.extent = {width, height};

        m_ViewportDirty = true;
    }

    void VulkanRendererAPI::SetClearColor(const glm::vec4& color)
    {
        m_ClearColor = color;
    }

    void VulkanRendererAPI::Clear()
    {
        // In Vulkan, clearing is done via render pass load operations or vkCmdClearAttachments.
        // This will be handled in the render pass begin.
        // For now, mark that a clear is requested.
        m_ClearRequested = true;
    }

    void VulkanRendererAPI::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
    {
        // In Vulkan, drawing requires:
        // 1. Active command buffer
        // 2. Bound pipeline
        // 3. Bound vertex/index buffers
        // 4. Bound descriptor sets

        // This abstraction doesn't map well to Vulkan's command buffer model.
        // For now, log that this is called but actual implementation requires
        // command buffer context which will be provided by the renderer.

        // TODO: Integrate with VulkanCommandBuffer when render loop is implemented
        ENGINE_CORE_TRACE("VulkanRendererAPI::DrawIndexed - requires command buffer context");
    }

    void VulkanRendererAPI::DrawArrays(uint32_t count, uint32_t first)
    {
        // Same as DrawIndexed - requires command buffer context
        ENGINE_CORE_TRACE("VulkanRendererAPI::DrawArrays({}, {})", count, first);
    }

    void VulkanRendererAPI::DrawArraysInstanced(uint32_t count, uint32_t instanceCount, uint32_t first)
    {
        ENGINE_CORE_TRACE("VulkanRendererAPI::DrawArraysInstanced({}, {}, {})", count, instanceCount, first);
    }

    void VulkanRendererAPI::DrawLines(uint32_t count, uint32_t first)
    {
        ENGINE_CORE_TRACE("VulkanRendererAPI::DrawLines({}, {})", count, first);
    }

    void VulkanRendererAPI::SetDepthTest(bool enable)
    {
        m_DepthTestEnabled = enable;
        // This will affect pipeline creation, not immediate state
    }

    void VulkanRendererAPI::SetDepthFunc(DepthFunc func)
    {
        m_DepthFunc = func;
        // This will affect pipeline creation
    }

    void VulkanRendererAPI::SetCullFace(bool enable)
    {
        m_CullFaceEnabled = enable;
    }

    void VulkanRendererAPI::SetCullFaceMode(CullFaceMode mode)
    {
        m_CullFaceMode = mode;
    }

    void VulkanRendererAPI::SetLineWidth(float width)
    {
        m_LineWidth = width;
        // Can be set dynamically via vkCmdSetLineWidth if wide lines feature is enabled
    }

    void VulkanRendererAPI::BindTextureUnit(uint32_t slot, uint32_t textureID)
    {
        // In Vulkan, textures are bound via descriptor sets, not texture units.
        // This needs to be handled differently through the shader/pipeline system.
        ENGINE_CORE_TRACE("VulkanRendererAPI::BindTextureUnit({}, {}) - use descriptor sets", slot, textureID);
    }

    void VulkanRendererAPI::BindCubemapUnit(uint32_t slot, uint32_t textureID)
    {
        ENGINE_CORE_TRACE("VulkanRendererAPI::BindCubemapUnit({}, {}) - use descriptor sets", slot, textureID);
    }

    void VulkanRendererAPI::ClearColorOnly()
    {
        m_ClearColorOnly = true;
    }

    int VulkanRendererAPI::GetBoundFramebufferID()
    {
        // In Vulkan, there's no concept of "bound framebuffer" in the same way.
        // Return the current framebuffer index from the context.
        return m_CurrentFramebufferID;
    }

    void VulkanRendererAPI::BindFramebufferByID(int id)
    {
        // Cache for later use in render pass
        m_CurrentFramebufferID = id;
    }

    void VulkanRendererAPI::DispatchCompute(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
    {
        // Compute dispatch requires:
        // 1. Active command buffer
        // 2. Bound compute pipeline
        // 3. Bound descriptor sets

        ENGINE_CORE_TRACE("VulkanRendererAPI::DispatchCompute({}, {}, {})", groupsX, groupsY, groupsZ);
    }

    void VulkanRendererAPI::MemoryBarrier(uint32_t barriers)
    {
        // In Vulkan, barriers are more explicit and require pipeline stages.
        // This will be implemented as a proper VkMemoryBarrier in command buffer.
        ENGINE_CORE_TRACE("VulkanRendererAPI::MemoryBarrier(0x{:X})", barriers);
    }

    void VulkanRendererAPI::DrawArraysIndirect(uint32_t bufferID)
    {
        ENGINE_CORE_TRACE("VulkanRendererAPI::DrawArraysIndirect({})", bufferID);
    }

    void VulkanRendererAPI::SetDepthMask(bool enable)
    {
        m_DepthWriteEnabled = enable;
    }

    void VulkanRendererAPI::SetBlendFunc(BlendFactor src, BlendFactor dst)
    {
        m_SrcBlendFactor = src;
        m_DstBlendFactor = dst;
    }

    // ========== Vulkan-specific helper methods ==========

    VkCompareOp VulkanRendererAPI::GetVkDepthCompareOp() const
    {
        switch (m_DepthFunc)
        {
        case DepthFunc::Less:
            return VK_COMPARE_OP_LESS;
        case DepthFunc::LEqual:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case DepthFunc::Greater:
            return VK_COMPARE_OP_GREATER;
        case DepthFunc::Always:
            return VK_COMPARE_OP_ALWAYS;
        }
        return VK_COMPARE_OP_LESS;
    }

    VkCullModeFlags VulkanRendererAPI::GetVkCullMode() const
    {
        if (!m_CullFaceEnabled)
            return VK_CULL_MODE_NONE;

        return m_CullFaceMode == CullFaceMode::Front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT;
    }

    VkBlendFactor VulkanRendererAPI::GetVkBlendFactor(BlendFactor factor)
    {
        switch (factor)
        {
        case BlendFactor::Zero:
            return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One:
            return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SrcAlpha:
            return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha:
            return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        }
        return VK_BLEND_FACTOR_ONE;
    }

    void VulkanRendererAPI::ApplyDynamicState(VkCommandBuffer cmd)
    {
        if (m_ViewportDirty)
        {
            vkCmdSetViewport(cmd, 0, 1, &m_Viewport);
            vkCmdSetScissor(cmd, 0, 1, &m_Scissor);
            m_ViewportDirty = false;
        }

        // Line width (requires VK_DYNAMIC_STATE_LINE_WIDTH in pipeline)
        // vkCmdSetLineWidth(cmd, m_LineWidth);
    }

} // namespace Engine
