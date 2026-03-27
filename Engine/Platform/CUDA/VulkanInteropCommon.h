#pragma once

// Vulkan <-> CUDA external interop 的跨平台公共工具（无 Vulkan/CUDA 头依赖）。
// 该头文件用于：
// 1) 同步值生成规则
// 2) 平台句柄类型映射
// 3) 导出句柄导入 CUDA 后的所有权规则

#include <cstdint>

namespace Engine
{
    namespace CudaInterop
    {
        enum class ExternalHandleType : uint8_t
        {
            OpaqueWin32 = 0,
            OpaqueFd    = 1
        };

        constexpr ExternalHandleType GetDefaultExternalHandleType()
        {
#ifdef _WIN32
            return ExternalHandleType::OpaqueWin32;
#else
            return ExternalHandleType::OpaqueFd;
#endif
        }

        constexpr const char* ToString(ExternalHandleType type)
        {
            switch (type)
            {
            case ExternalHandleType::OpaqueWin32:
                return "OpaqueWin32";
            case ExternalHandleType::OpaqueFd:
                return "OpaqueFd";
            default:
                return "Unknown";
            }
        }

        struct InteropFrameSyncValues
        {
            uint64_t CudaSignalValue   = 1;
            uint64_t VulkanSignalValue = 2;
            uint64_t WaitVulkanValue   = 0;
        };

        // 约定：
        //   frame N: cudaSignal = 2N + 1, vkSignal = 2N + 2
        //   frame N 启动前，CUDA 等待 frame N-1 的 vkSignal（N=0 时等待 0）
        constexpr InteropFrameSyncValues BuildInteropFrameSyncValues(uint64_t frameIndex)
        {
            InteropFrameSyncValues out{};
            out.CudaSignalValue   = frameIndex * 2ull + 1ull;
            out.VulkanSignalValue = frameIndex * 2ull + 2ull;
            out.WaitVulkanValue   = (frameIndex == 0ull) ? 0ull : ((frameIndex - 1ull) * 2ull + 2ull);
            return out;
        }

#ifdef _WIN32
        using NativeInteropHandle = void*;
        constexpr NativeInteropHandle kInvalidInteropHandle = nullptr;
#else
        using NativeInteropHandle = int;
        constexpr NativeInteropHandle kInvalidInteropHandle = -1;
#endif

        struct OwnedInteropHandle
        {
            NativeInteropHandle Value = kInvalidInteropHandle;
            bool                ImportedByCuda = false;

            constexpr bool IsValid() const
            {
                return Value != kInvalidInteropHandle;
            }

            constexpr void MarkImportedByCuda()
            {
                ImportedByCuda = true;
                Value          = kInvalidInteropHandle;
            }
        };

        constexpr bool IsKnownExternalHandleType(ExternalHandleType type)
        {
            switch (type)
            {
            case ExternalHandleType::OpaqueWin32:
            case ExternalHandleType::OpaqueFd:
                return true;
            default:
                return false;
            }
        }

        // Vulkan 导出的外部句柄在成功导入 CUDA 后，导出方句柄不再持有所有权。
        constexpr bool ShouldCloseHandleAfterCudaImport(ExternalHandleType type)
        {
            switch (type)
            {
            case ExternalHandleType::OpaqueWin32:
            case ExternalHandleType::OpaqueFd:
                return true;
            default:
                return false;
            }
        }

    } // namespace CudaInterop
} // namespace Engine

