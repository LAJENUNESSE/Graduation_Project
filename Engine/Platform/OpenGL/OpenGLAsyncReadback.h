#pragma once

#include "Renderer/GPUAsyncReadback.h"

namespace Engine
{

    class OpenGLAsyncReadback : public GPUAsyncReadback
    {
    public:
        explicit OpenGLAsyncReadback(uint32_t size);
        ~OpenGLAsyncReadback() override;

        void CopyFrom(const Ref<ShaderStorageBuffer>& src, uint32_t size, uint32_t srcOffset = 0) override;
        bool IsReady() const override;
        void GetData(void* dest, uint32_t size) override;
        void Reset() override;
        bool IsPending() const override;

    private:
        uint32_t m_StagingBuffer = 0;
        uint32_t m_Size          = 0;
        void*    m_Fence         = nullptr;
        bool     m_Pending       = false;
    };

} // namespace Engine
