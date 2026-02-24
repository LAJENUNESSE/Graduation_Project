#pragma once

#include "Renderer/RendererAPI.h"

namespace Engine
{

    class OpenGLRendererAPI : public RendererAPI
    {
    public:
        void Init() override;
        void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        void SetClearColor(const glm::vec4& color) override;
        void Clear() override;
        void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) override;

        void DrawArrays(uint32_t count, uint32_t first = 0) override;
        void DrawLines(uint32_t count, uint32_t first = 0) override;
        void SetDepthTest(bool enable) override;
        void SetDepthFunc(DepthFunc func) override;
        void SetCullFace(bool enable) override;
        void SetCullFaceMode(CullFaceMode mode) override;
        void SetLineWidth(float width) override;
        void BindTextureUnit(uint32_t slot, uint32_t textureID) override;
        void ClearColorOnly() override;
        int GetBoundFramebufferID() override;
        void BindFramebufferByID(int id) override;
    };

} // namespace Engine
