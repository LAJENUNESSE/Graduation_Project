#pragma once

#include "Core/Base.h"
#include "Renderer/VertexArray.h"

#include <glm/glm.hpp>

namespace Engine
{

    enum class DepthFunc { Less, LEqual, Greater, Always };
    enum class CullFaceMode { Front, Back };
    enum class BlendFactor { Zero = 0, One, SrcAlpha, OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha };

    namespace BarrierBit
    {
        constexpr uint32_t ShaderStorage = 0x00002000;  // GL_SHADER_STORAGE_BARRIER_BIT
        constexpr uint32_t Command       = 0x00000040;  // GL_COMMAND_BARRIER_BIT
        constexpr uint32_t BufferUpdate  = 0x00000200;  // GL_BUFFER_UPDATE_BARRIER_BIT
        constexpr uint32_t All           = 0xFFFFFFFF;  // GL_ALL_BARRIER_BITS
    }

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

        // Compute Shader
        virtual void DispatchCompute(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) = 0;
        virtual void MemoryBarrier(uint32_t barriers) = 0;

        // Indirect Draw
        virtual void DrawArraysIndirect(uint32_t bufferID) = 0;

        // Blend / Depth
        virtual void SetDepthMask(bool enable) = 0;
        virtual void SetBlendFunc(BlendFactor src, BlendFactor dst) = 0;

        static API GetAPI()
        {
            return s_API;
        }

    private:
        static API s_API;
    };

} // namespace Engine
