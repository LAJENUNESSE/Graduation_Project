#pragma once

#include "Renderer/RendererAPI.h"

#include <vulkan/vulkan.h>

namespace Engine
{

    /**
     * VulkanRendererAPI provides a Vulkan implementation of the RendererAPI interface.
     *
     * Unlike OpenGL's immediate-mode state machine, Vulkan requires command buffers and
     * explicit pipeline state objects. This class caches render state and provides helper
     * methods for command buffer recording.
     *
     * Usage pattern:
     * 1. RendererAPI calls (SetViewport, SetDepthTest, etc.) cache state
     * 2. When recording commands, use GetVk*() helpers to query cached state
     * 3. Call ApplyDynamicState() to set viewport/scissor on command buffer
     */
    class VulkanRendererAPI : public RendererAPI
    {
    public:
        void Init() override;
        void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        void SetClearColor(const glm::vec4& color) override;
        void Clear() override;
        void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) override;

        void DrawArrays(uint32_t count, uint32_t first = 0) override;
        void DrawArraysInstanced(uint32_t count, uint32_t instanceCount, uint32_t first = 0) override;
        void DrawLines(uint32_t count, uint32_t first = 0) override;
        void SetDepthTest(bool enable) override;
        void SetDepthFunc(DepthFunc func) override;
        void SetCullFace(bool enable) override;
        void SetCullFaceMode(CullFaceMode mode) override;
        void SetLineWidth(float width) override;
        void BindTextureUnit(uint32_t slot, uint32_t textureID) override;
        void BindCubemapUnit(uint32_t slot, uint32_t textureID) override;
        void ClearColorOnly() override;
        int  GetBoundFramebufferID() override;
        void BindFramebufferByID(int id) override;
        void DispatchCompute(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) override;
        void MemoryBarrier(uint32_t barriers) override;
        void DrawArraysIndirect(uint32_t bufferID) override;
        void SetDepthMask(bool enable) override;
        void SetBlendFunc(BlendFactor src, BlendFactor dst) override;

        // ========== Vulkan-specific accessors ==========

        // Get cached clear color as VkClearColorValue
        const glm::vec4& GetClearColor() const { return m_ClearColor; }
        bool             IsClearRequested() const { return m_ClearRequested; }
        void             ClearClearRequest() { m_ClearRequested = false; }

        // Pipeline state helpers (for VulkanPipeline construction)
        bool            IsDepthTestEnabled() const { return m_DepthTestEnabled; }
        bool            IsDepthWriteEnabled() const { return m_DepthWriteEnabled; }
        VkCompareOp     GetVkDepthCompareOp() const;
        VkCullModeFlags GetVkCullMode() const;
        bool            IsBlendEnabled() const { return m_BlendEnabled; }
        VkBlendFactor   GetVkSrcBlendFactor() const { return GetVkBlendFactor(m_SrcBlendFactor); }
        VkBlendFactor   GetVkDstBlendFactor() const { return GetVkBlendFactor(m_DstBlendFactor); }

        // Viewport/scissor (for dynamic state)
        const VkViewport& GetViewport() const { return m_Viewport; }
        const VkRect2D&   GetScissor() const { return m_Scissor; }

        // Apply dynamic state to command buffer
        void ApplyDynamicState(VkCommandBuffer cmd);

        // Convert BlendFactor enum to Vulkan equivalent
        static VkBlendFactor GetVkBlendFactor(BlendFactor factor);

    private:
        // Clear state
        glm::vec4 m_ClearColor     = {0.0f, 0.0f, 0.0f, 1.0f};
        bool      m_ClearRequested = false;
        bool      m_ClearColorOnly = false;

        // Viewport state
        VkViewport m_Viewport      = {};
        VkRect2D   m_Scissor       = {};
        bool       m_ViewportDirty = false;

        // Depth state
        bool      m_DepthTestEnabled  = true;
        bool      m_DepthWriteEnabled = true;
        DepthFunc m_DepthFunc         = DepthFunc::Less;

        // Cull state
        bool         m_CullFaceEnabled = true;
        CullFaceMode m_CullFaceMode    = CullFaceMode::Back;

        // Blend state
        bool        m_BlendEnabled   = true;
        BlendFactor m_SrcBlendFactor = BlendFactor::SrcAlpha;
        BlendFactor m_DstBlendFactor = BlendFactor::OneMinusSrcAlpha;

        // Line state
        float m_LineWidth = 1.0f;

        // Framebuffer tracking
        int m_CurrentFramebufferID = 0;
    };

} // namespace Engine
