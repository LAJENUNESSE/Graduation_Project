#pragma once

#include "Renderer/StorageBuffer.h"

namespace Engine
{

    class OpenGLStorageBuffer : public ShaderStorageBuffer
    {
    public:
        OpenGLStorageBuffer(uint32_t size, uint32_t binding);
        OpenGLStorageBuffer(const void* data, uint32_t size, uint32_t binding);
        // GPU-only immutable storage
        OpenGLStorageBuffer(uint32_t size, uint32_t binding, bool gpuOnly);
        OpenGLStorageBuffer(const void* data, uint32_t size, uint32_t binding, bool gpuOnly);
        ~OpenGLStorageBuffer() override;

        void Bind(uint32_t binding) const override;
        void Unbind() const override;

        void SetData(const void* data, uint32_t size, uint32_t offset = 0) override;
        void GetData(void* data, uint32_t size, uint32_t offset = 0) const override;

        uint32_t GetRendererID() const override { return m_RendererID; }
        uint32_t GetSize() const override { return m_Size; }

    private:
        uint32_t m_RendererID = 0;
        uint32_t m_Size       = 0;
        bool     m_Immutable  = false;
    };

} // namespace Engine
