#pragma once

#include "Core/Base.h"

#include <cstdint>

namespace Engine
{

    class ShaderStorageBuffer;

    // Platform-agnostic async GPU → CPU readback.
    // Encapsulates a staging buffer + fence for non-blocking counter reads.
    class GPUAsyncReadback
    {
    public:
        virtual ~GPUAsyncReadback() = default;

        // Initiate async copy from SSBO into internal staging buffer.
        virtual void CopyFrom(const Ref<ShaderStorageBuffer>& src, uint32_t size, uint32_t srcOffset = 0) = 0;

        // Non-blocking check: has the last CopyFrom completed on the GPU?
        virtual bool IsReady() const = 0;

        // Read data from staging buffer (only valid after IsReady() == true).
        virtual void GetData(void* dest, uint32_t size) = 0;

        // Reset state: delete pending fence, mark as not pending.
        virtual void Reset() = 0;

        // True if CopyFrom has been called but GetData has not consumed the result.
        virtual bool IsPending() const = 0;

        static Ref<GPUAsyncReadback> Create(uint32_t size);
    };

} // namespace Engine
