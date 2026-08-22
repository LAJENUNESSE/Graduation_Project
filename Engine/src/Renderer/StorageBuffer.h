#pragma once

#include "Core/Base.h"
#include "Debug/GpuMemCategory.h"

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

        // Clear entire buffer to zero (GPU-side, no CPU allocation)
        virtual void ClearToZero() = 0;

        // memCategory 用于显存监控分类：粒子系统传 Particle，其余保持默认
        static Ref<ShaderStorageBuffer>
        Create(uint32_t size, uint32_t binding, GpuMemCategory memCategory = GpuMemCategory::UniformStorage);
        static Ref<ShaderStorageBuffer> Create(const void*    data,
                                               uint32_t       size,
                                               uint32_t       binding,
                                               GpuMemCategory memCategory = GpuMemCategory::UniformStorage);

        // GPU-only immutable storage (glBufferStorage flags=0, no CPU read/write after init)
        static Ref<ShaderStorageBuffer>
        CreateGPUOnly(uint32_t size, uint32_t binding, GpuMemCategory memCategory = GpuMemCategory::UniformStorage);
        static Ref<ShaderStorageBuffer> CreateGPUOnly(const void*    data,
                                                      uint32_t       size,
                                                      uint32_t       binding,
                                                      GpuMemCategory memCategory = GpuMemCategory::UniformStorage);

        // GPU dynamic storage (glBufferStorage + GL_DYNAMIC_STORAGE_BIT)
        // CUDA interop 兼容：告知驱动内容会变化，避免 immutable 优化导致 CUDA 写入不可见
        static Ref<ShaderStorageBuffer>
        CreateGPUDynamic(uint32_t size, uint32_t binding, GpuMemCategory memCategory = GpuMemCategory::UniformStorage);
        static Ref<ShaderStorageBuffer> CreateGPUDynamic(const void*    data,
                                                         uint32_t       size,
                                                         uint32_t       binding,
                                                         GpuMemCategory memCategory = GpuMemCategory::UniformStorage);
    };

} // namespace Engine
