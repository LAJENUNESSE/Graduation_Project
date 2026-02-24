#pragma once

#include "Core/Base.h"
#include "Renderer/VertexArray.h"

#include <glm/glm.hpp>

namespace Engine
{

    enum class DepthFunc { Less, LEqual, Greater, Always };
    enum class CullFaceMode { Front, Back };

    class RendererAPI
    {
    public:
        enum class API
        {
            None = 0,
            OpenGL = 1
        };

    public:
        virtual ~RendererAPI() = default;

        virtual void Init() = 0;
        virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
        virtual void SetClearColor(const glm::vec4& color) = 0;
        virtual void Clear() = 0;
        virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) = 0;

        virtual void DrawArrays(uint32_t count, uint32_t first = 0) = 0;
        virtual void DrawLines(uint32_t count, uint32_t first = 0) = 0;
        virtual void SetDepthTest(bool enable) = 0;
        virtual void SetDepthFunc(DepthFunc func) = 0;
        virtual void SetCullFace(bool enable) = 0;
        virtual void SetCullFaceMode(CullFaceMode mode) = 0;
        virtual void SetLineWidth(float width) = 0;
        virtual void BindTextureUnit(uint32_t slot, uint32_t textureID) = 0;
        virtual void ClearColorOnly() = 0;
        virtual int GetBoundFramebufferID() = 0;
        virtual void BindFramebufferByID(int id) = 0;

        static API GetAPI()
        {
            return s_API;
        }

    private:
        static API s_API;
    };

} // namespace Engine
