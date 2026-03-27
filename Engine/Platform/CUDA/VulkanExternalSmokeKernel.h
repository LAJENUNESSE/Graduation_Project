#pragma once

// VulkanExternalSmokeKernel 声明
// 最小 CUDA kernel，用于验证 Vulkan-CUDA interop 管线
// 仅写共享 buffer，不依赖业务数据

#include <cstddef>
#include <cstdint>

namespace Engine
{
    namespace CudaInterop
    {
        /// 启动 smoke kernel，向 mappedBuffer 写入确定性数据用于验证 interop 闭环。
        /// @param mappedBuffer CUDA 映射的设备指针（来自 CudaExternalInteropContext::GetMappedDevicePointer()）
        /// @param bufferBytes  缓冲区大小（字节）
        /// @param frameIndex   当前帧索引，用于生成确定性数据
        /// @param stream       CUDA stream（来自 CudaExternalInteropContext::GetStream()）
        void LaunchVulkanExternalSmokeKernel(void* mappedBuffer, size_t bufferBytes, uint64_t frameIndex, void* stream);

    } // namespace CudaInterop
} // namespace Engine
