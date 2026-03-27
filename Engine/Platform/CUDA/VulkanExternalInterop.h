#pragma once

#include "Platform/CUDA/VulkanInteropCommon.h"

#include <cstddef>
#include <cstdint>

#include <cuda_runtime_api.h>

// 在包含 vulkan.h 之前定义平台宏以获取 Win32 扩展
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

namespace Engine
{
    namespace CudaInterop
    {
        class VulkanExternalExport
        {
        public:
            static VkExternalMemoryHandleTypeFlagBits
            VkMemoryHandleType(ExternalHandleType handleType = GetDefaultExternalHandleType());
            static VkExternalSemaphoreHandleTypeFlagBits
            VkSemaphoreHandleType(ExternalHandleType handleType = GetDefaultExternalHandleType());

            static cudaExternalMemoryHandleType
            CudaMemoryHandleType(ExternalHandleType handleType = GetDefaultExternalHandleType());
            static cudaExternalSemaphoreHandleType
            CudaSemaphoreHandleType(ExternalHandleType handleType = GetDefaultExternalHandleType());

            static OwnedInteropHandle ExportMemory(VkDevice           device,
                                                   VkDeviceMemory     memory,
                                                   ExternalHandleType handleType = GetDefaultExternalHandleType());
            static OwnedInteropHandle ExportTimelineSemaphore(
                VkDevice device, VkSemaphore semaphore, ExternalHandleType handleType = GetDefaultExternalHandleType());
        };

        class CudaExternalInteropContext
        {
        public:
            struct InitDesc
            {
                OwnedInteropHandle MemoryHandle;
                OwnedInteropHandle SemaphoreHandle;
                size_t             SharedBufferSize = 0;
                int                CudaDeviceIndex  = 0;
                ExternalHandleType HandleType       = GetDefaultExternalHandleType();
            };

            CudaExternalInteropContext() = default;
            ~CudaExternalInteropContext();

            CudaExternalInteropContext(const CudaExternalInteropContext&)            = delete;
            CudaExternalInteropContext& operator=(const CudaExternalInteropContext&) = delete;

            void Initialize(InitDesc initDesc);
            void Destroy();

            void WaitTimelineValue(uint64_t waitValue);
            void SignalTimelineValue(uint64_t signalValue);
            void SyncStream();

            void* GetMappedDevicePointer() const { return m_MappedBuffer; }
            void* GetStream() const { return m_Stream; }

        private:
            static void CheckCuda(cudaError_t err, const char* what);
            static void CloseNativeHandle(OwnedInteropHandle& handle, ExternalHandleType handleType);

        private:
            cudaExternalMemory_t    m_ExternalMemory    = nullptr;
            cudaExternalSemaphore_t m_ExternalSemaphore = nullptr;
            void*                   m_MappedBuffer      = nullptr;
            cudaStream_t            m_Stream            = nullptr;
            ExternalHandleType      m_HandleType        = GetDefaultExternalHandleType();
        };
    } // namespace CudaInterop
} // namespace Engine
