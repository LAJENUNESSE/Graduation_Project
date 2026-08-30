#pragma once

#include "Renderer/UniformBuffer.h"

namespace Engine
{

    class OpenGLUniformBuffer : public UniformBuffer
    {
    public:
        OpenGLUniformBuffer(uint32_t size, uint32_t binding);
        ~OpenGLUniformBuffer() override;

        void SetData(const void* data, uint32_t size, uint32_t offset = 0) override;
        // 构造时已 glBindBufferBase 固定 binding；此方法按需重绑（幂等）
        void Bind(uint32_t binding) const override;

    private:
        uint32_t m_RendererID = 0;
    };

} // namespace Engine
