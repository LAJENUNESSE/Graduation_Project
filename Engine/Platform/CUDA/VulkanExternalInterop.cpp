#include "Platform/CUDA/VulkanExternalInterop.h"

#ifdef _WIN32
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif
#define NOMINMAX
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#else
#include <unistd.h>
#endif

#include <sstream>
#include <stdexcept>

namespace Engine
{
    namespace CudaInterop
    {
        namespace
        {
            [[noreturn]] void ThrowVk(VkResult result, const char* what)
            {
                std::ostringstream oss;
                oss << what << " (VkResult=" << static_cast<int>(result) << ")";
                throw std::runtime_error(oss.str());
            }

            void CheckVk(VkResult result, const char* what)
            {
                if (result != VK_SUCCESS)
                    ThrowVk(result, what);
            }
        } // namespace

        VkExternalMemoryHandleTypeFlagBits VulkanExternalExport::VkMemoryHandleType(ExternalHandleType handleType)
        {
            switch (handleType)
            {
            case ExternalHandleType::OpaqueWin32:
                return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
            case ExternalHandleType::OpaqueFd:
                return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
            default:
                throw std::runtime_error("Unsupported external memory handle type");
            }
        }

        VkExternalSemaphoreHandleTypeFlagBits VulkanExternalExport::VkSemaphoreHandleType(ExternalHandleType handleType)
        {
            switch (handleType)
            {
            case ExternalHandleType::OpaqueWin32:
                return VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
            case ExternalHandleType::OpaqueFd:
                return VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
            default:
                throw std::runtime_error("Unsupported external semaphore handle type");
            }
        }

        cudaExternalMemoryHandleType VulkanExternalExport::CudaMemoryHandleType(ExternalHandleType handleType)
        {
            switch (handleType)
            {
            case ExternalHandleType::OpaqueWin32:
                return cudaExternalMemoryHandleTypeOpaqueWin32;
            case ExternalHandleType::OpaqueFd:
                return cudaExternalMemoryHandleTypeOpaqueFd;
            default:
                throw std::runtime_error("Unsupported CUDA external memory handle type");
            }
        }

        cudaExternalSemaphoreHandleType VulkanExternalExport::CudaSemaphoreHandleType(ExternalHandleType handleType)
        {
            switch (handleType)
            {
            case ExternalHandleType::OpaqueWin32:
                return cudaExternalSemaphoreHandleTypeTimelineSemaphoreWin32;
            case ExternalHandleType::OpaqueFd:
                return cudaExternalSemaphoreHandleTypeTimelineSemaphoreFd;
            default:
                throw std::runtime_error("Unsupported CUDA external semaphore handle type");
            }
        }

        OwnedInteropHandle VulkanExternalExport::ExportMemory(VkDevice           device,
                                                              VkDeviceMemory     memory,
                                                              ExternalHandleType handleType)
        {
            OwnedInteropHandle out{};
#ifdef _WIN32
            const auto pfn = reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(
                vkGetDeviceProcAddr(device, "vkGetMemoryWin32HandleKHR"));
            if (!pfn)
                throw std::runtime_error("vkGetMemoryWin32HandleKHR is unavailable");

            VkMemoryGetWin32HandleInfoKHR info{};
            info.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
            info.memory     = memory;
            info.handleType = VkMemoryHandleType(handleType);

            HANDLE handle = nullptr;
            CheckVk(pfn(device, &info, &handle), "vkGetMemoryWin32HandleKHR");
            out.Value = handle;
#else
            const auto pfn = reinterpret_cast<PFN_vkGetMemoryFdKHR>(vkGetDeviceProcAddr(device, "vkGetMemoryFdKHR"));
            if (!pfn)
                throw std::runtime_error("vkGetMemoryFdKHR is unavailable");

            VkMemoryGetFdInfoKHR info{};
            info.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
            info.memory     = memory;
            info.handleType = VkMemoryHandleType(handleType);

            int fd = -1;
            CheckVk(pfn(device, &info, &fd), "vkGetMemoryFdKHR");
            out.Value = fd;
#endif
            return out;
        }

        OwnedInteropHandle VulkanExternalExport::ExportTimelineSemaphore(VkDevice           device,
                                                                         VkSemaphore        semaphore,
                                                                         ExternalHandleType handleType)
        {
            OwnedInteropHandle out{};
#ifdef _WIN32
            const auto pfn = reinterpret_cast<PFN_vkGetSemaphoreWin32HandleKHR>(
                vkGetDeviceProcAddr(device, "vkGetSemaphoreWin32HandleKHR"));
            if (!pfn)
                throw std::runtime_error("vkGetSemaphoreWin32HandleKHR is unavailable");

            VkSemaphoreGetWin32HandleInfoKHR info{};
            info.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
            info.semaphore  = semaphore;
            info.handleType = VkSemaphoreHandleType(handleType);

            HANDLE handle = nullptr;
            CheckVk(pfn(device, &info, &handle), "vkGetSemaphoreWin32HandleKHR");
            out.Value = handle;
#else
            const auto pfn =
                reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(vkGetDeviceProcAddr(device, "vkGetSemaphoreFdKHR"));
            if (!pfn)
                throw std::runtime_error("vkGetSemaphoreFdKHR is unavailable");

            VkSemaphoreGetFdInfoKHR info{};
            info.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
            info.semaphore  = semaphore;
            info.handleType = VkSemaphoreHandleType(handleType);

            int fd = -1;
            CheckVk(pfn(device, &info, &fd), "vkGetSemaphoreFdKHR");
            out.Value = fd;
#endif
            return out;
        }

        CudaExternalInteropContext::~CudaExternalInteropContext()
        {
            Destroy();
        }

        void CudaExternalInteropContext::CheckCuda(cudaError_t err, const char* what)
        {
            if (err == cudaSuccess)
                return;

            std::ostringstream oss;
            oss << what << " failed: " << cudaGetErrorString(err);
            throw std::runtime_error(oss.str());
        }

        void CudaExternalInteropContext::CloseNativeHandle(OwnedInteropHandle& handle, ExternalHandleType handleType)
        {
            if (!handle.IsValid() || !ShouldCloseHandleAfterCudaImport(handleType))
                return;

#ifdef _WIN32
            CloseHandle(static_cast<HANDLE>(handle.Value));
#else
            ::close(handle.Value);
#endif
            handle.MarkImportedByCuda();
        }

        void CudaExternalInteropContext::Initialize(InitDesc initDesc)
        {
            if (m_Stream || m_ExternalMemory || m_ExternalSemaphore || m_MappedBuffer)
                throw std::runtime_error("CudaExternalInteropContext already initialized");
            if (!initDesc.MemoryHandle.IsValid() || !initDesc.SemaphoreHandle.IsValid())
                throw std::runtime_error("Invalid Vulkan external handles for CUDA import");
            if (initDesc.SharedBufferSize == 0)
                throw std::runtime_error("Shared Vulkan buffer size must be > 0");
            if (!IsKnownExternalHandleType(initDesc.HandleType))
                throw std::runtime_error("Unknown external handle type");

            m_HandleType = initDesc.HandleType;

            CheckCuda(cudaSetDevice(initDesc.CudaDeviceIndex), "cudaSetDevice");
            CheckCuda(cudaStreamCreate(&m_Stream), "cudaStreamCreate");

            cudaExternalMemoryHandleDesc memDesc{};
            memDesc.type = VulkanExternalExport::CudaMemoryHandleType(m_HandleType);
#ifdef _WIN32
            memDesc.handle.win32.handle = static_cast<HANDLE>(initDesc.MemoryHandle.Value);
#else
            memDesc.handle.fd = initDesc.MemoryHandle.Value;
#endif
            memDesc.size = initDesc.SharedBufferSize;
            CheckCuda(cudaImportExternalMemory(&m_ExternalMemory, &memDesc), "cudaImportExternalMemory");
            CloseNativeHandle(initDesc.MemoryHandle, m_HandleType);

            cudaExternalMemoryBufferDesc bufferDesc{};
            bufferDesc.offset = 0;
            bufferDesc.size   = initDesc.SharedBufferSize;
            CheckCuda(cudaExternalMemoryGetMappedBuffer(&m_MappedBuffer, m_ExternalMemory, &bufferDesc),
                      "cudaExternalMemoryGetMappedBuffer");

            cudaExternalSemaphoreHandleDesc semDesc{};
            semDesc.type = VulkanExternalExport::CudaSemaphoreHandleType(m_HandleType);
#ifdef _WIN32
            semDesc.handle.win32.handle = static_cast<HANDLE>(initDesc.SemaphoreHandle.Value);
#else
            semDesc.handle.fd = initDesc.SemaphoreHandle.Value;
#endif
            CheckCuda(cudaImportExternalSemaphore(&m_ExternalSemaphore, &semDesc), "cudaImportExternalSemaphore");
            CloseNativeHandle(initDesc.SemaphoreHandle, m_HandleType);
        }

        void CudaExternalInteropContext::Destroy()
        {
            if (m_MappedBuffer)
            {
                cudaFree(m_MappedBuffer);
                m_MappedBuffer = nullptr;
            }

            if (m_ExternalSemaphore)
            {
                cudaDestroyExternalSemaphore(m_ExternalSemaphore);
                m_ExternalSemaphore = nullptr;
            }

            if (m_ExternalMemory)
            {
                cudaDestroyExternalMemory(m_ExternalMemory);
                m_ExternalMemory = nullptr;
            }

            if (m_Stream)
            {
                cudaStreamDestroy(m_Stream);
                m_Stream = nullptr;
            }
        }

        void CudaExternalInteropContext::WaitTimelineValue(uint64_t waitValue)
        {
            if (!m_ExternalSemaphore || !m_Stream)
                throw std::runtime_error("CudaExternalInteropContext is not initialized");

            cudaExternalSemaphoreWaitParams waitParams{};
            waitParams.params.fence.value = waitValue;
            CheckCuda(cudaWaitExternalSemaphoresAsync(&m_ExternalSemaphore, &waitParams, 1, m_Stream),
                      "cudaWaitExternalSemaphoresAsync");
        }

        void CudaExternalInteropContext::SignalTimelineValue(uint64_t signalValue)
        {
            if (!m_ExternalSemaphore || !m_Stream)
                throw std::runtime_error("CudaExternalInteropContext is not initialized");

            cudaExternalSemaphoreSignalParams signalParams{};
            signalParams.params.fence.value = signalValue;
            CheckCuda(cudaSignalExternalSemaphoresAsync(&m_ExternalSemaphore, &signalParams, 1, m_Stream),
                      "cudaSignalExternalSemaphoresAsync");
        }

        void CudaExternalInteropContext::SyncStream()
        {
            if (!m_Stream)
                return;
            CheckCuda(cudaStreamSynchronize(m_Stream), "cudaStreamSynchronize");
        }

    } // namespace CudaInterop
} // namespace Engine

