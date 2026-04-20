#pragma once

// Stub — Vulkan RendererAPI implementation will be added in Phase 3.

#include "Renderer/RendererAPI.h"

namespace Engine
{

    class VulkanRendererAPI : public RendererAPI
    {
    public:
        void Init() override {}
        void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override
        {
            (void)x; (void)y; (void)width; (void)height;
        }
        void SetClearColor(const glm::vec4& color) override { (void)color; }
        void Clear() override {}
        void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) override
        {
            (void)vertexArray; (void)indexCount;
        }

        void DrawArrays(uint32_t count, uint32_t first = 0) override { (void)count; (void)first; }
        void DrawArraysInstanced(uint32_t count, uint32_t instanceCount, uint32_t first = 0) override
        {
            (void)count; (void)instanceCount; (void)first;
        }
        void DrawLines(uint32_t count, uint32_t first = 0) override { (void)count; (void)first; }
        void SetDepthTest(bool enable) override { (void)enable; }
        void SetDepthFunc(DepthFunc func) override { (void)func; }
        void SetCullFace(bool enable) override { (void)enable; }
        void SetCullFaceMode(CullFaceMode mode) override { (void)mode; }
        void SetLineWidth(float width) override { (void)width; }
        void BindTextureUnit(uint32_t slot, uint32_t textureID) override { (void)slot; (void)textureID; }
        void BindCubemapUnit(uint32_t slot, uint32_t textureID) override { (void)slot; (void)textureID; }
        void ClearColorOnly() override {}
        int  GetBoundFramebufferID() override { return 0; }
        void BindFramebufferByID(int id) override { (void)id; }

        void DispatchCompute(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) override
        {
            (void)groupsX; (void)groupsY; (void)groupsZ;
        }
        void MemoryBarrier(uint32_t barriers) override { (void)barriers; }

        void DrawArraysIndirect(uint32_t bufferID) override { (void)bufferID; }

        void SetDepthMask(bool enable) override { (void)enable; }
        void SetBlendFunc(BlendFactor src, BlendFactor dst) override { (void)src; (void)dst; }
        void SetBlend(bool enable) override { (void)enable; }
        bool GetBlendEnabled() override { return false; }
        void SetScissorTest(bool enable) override { (void)enable; }
        void SetColorMask(bool r, bool g, bool b, bool a) override { (void)r; (void)g; (void)b; (void)a; }

        glm::vec4 GetClearColor() override { return glm::vec4(0.0f); }

        void SetReadBuffer(uint32_t attachment) override { (void)attachment; }
        void SetDrawBuffer(uint32_t attachment) override { (void)attachment; }
        void SetDrawBuffers(uint32_t count, const uint32_t* attachments) override { (void)count; (void)attachments; }
        void CopyFramebufferToTexture(uint32_t texID, uint32_t width, uint32_t height) override
        {
            (void)texID; (void)width; (void)height;
        }

        void QueryCapabilities(RendererCapabilities& caps) override { (void)caps; }
    };

} // namespace Engine
