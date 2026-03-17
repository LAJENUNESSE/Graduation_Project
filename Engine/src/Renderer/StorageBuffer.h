#pragma once

#include "Core/Base.h"

#include <cstdint>

namespace Engine
{

    class ShaderStorageBuffer
    {
    public:
        virtual ~ShaderStorageBuffer() = default;

        virtual void Bind(uint32_t binding) const = 0;
        virtual void Unbind() const               = 0;

        virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0) = 0;
        virtual void GetData(void* data, uint32_t size, uint32_t offset = 0) const = 0;

        virtual uint32_t GetRendererID() const = 0;
        virtual uint32_t GetSize() const       = 0;

        static Ref<ShaderStorageBuffer> Create(uint32_t size, uint32_t binding);
        static Ref<ShaderStorageBuffer> Create(const void* data, uint32_t size, uint32_t binding);

        // GPU-only immutable storage (glBufferStorage, no CPU read/write after init)
        static Ref<ShaderStorageBuffer> CreateGPUOnly(uint32_t size, uint32_t binding);
        static Ref<ShaderStorageBuffer> CreateGPUOnly(const void* data, uint32_t size, uint32_t binding);
    };

} // namespace Engine
