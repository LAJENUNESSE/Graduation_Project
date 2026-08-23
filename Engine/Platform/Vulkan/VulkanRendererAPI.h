#pragma once

#include "Renderer/RendererAPI.h"

namespace Engine
{

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
        // Phase 8.2：view/sampler 直通写场景状态机纹理槽（PBR IBL / 阴影图绑定路径）
        void BindTextureView(uint32_t slot, void* view, void* sampler) override;
        void BindCubemapView(uint32_t slot, void* view, void* sampler) override;
        void ClearColorOnly() override;
        int  GetBoundFramebufferID() override;
        void BindFramebufferByID(int id) override;

        void DispatchCompute(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) override;
        void MemoryBarrier(uint32_t barriers) override;
        void WaitIdle() override;

        void DrawArraysIndirect(uint32_t bufferID) override;

        void SetDepthMask(bool enable) override;
        void SetBlendFunc(BlendFactor src, BlendFactor dst) override;
        void SetBlend(bool enable) override;
        bool GetBlendEnabled() override;
        void SetScissorTest(bool enable) override;
        void SetColorMask(bool r, bool g, bool b, bool a) override;

        glm::vec4 GetClearColor() override;

        void SetReadBuffer(uint32_t attachment) override;
        void SetDrawBuffer(uint32_t attachment) override;
        void SetDrawBuffers(uint32_t count, const uint32_t* attachments) override;
        void CopyFramebufferToTexture(uint32_t texID, uint32_t width, uint32_t height) override;

        void QueryCapabilities(RendererCapabilities& caps) override;

    private:
        glm::vec4    m_ClearColor         = {0.392f, 0.584f, 0.929f, 1.0f};
        bool         m_BlendEnabled       = true;
        bool         m_DepthTestEnabled   = true;
        bool         m_DepthMaskEnabled   = true;
        bool         m_CullFaceEnabled    = false;
        bool         m_ScissorTestEnabled = false;
        bool         m_ColorMaskR         = true;
        bool         m_ColorMaskG         = true;
        bool         m_ColorMaskB         = true;
        bool         m_ColorMaskA         = true;
        DepthFunc    m_DepthFunc          = DepthFunc::Less;
        CullFaceMode m_CullFaceMode       = CullFaceMode::Back;
        BlendFactor  m_BlendSrc           = BlendFactor::SrcAlpha;
        BlendFactor  m_BlendDst           = BlendFactor::OneMinusSrcAlpha;
        float        m_LineWidth          = 1.0f;
    };

} // namespace Engine
