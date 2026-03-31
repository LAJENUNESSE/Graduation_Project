#pragma once

// CUDA-Vulkan 互操作资源管理器（多槽位）。
//
// 拥有：VkBuffer + VkDeviceMemory（外部内存）、Timeline Semaphore、CUDA 导入句柄、CUDA 流。
// 不拥有：它使用的 VkDevice/VkPhysicalDevice（由 VulkanContext 拥有）。
//
// 设计目标：提供与 CudaGLInteropContext 相同的接口（RegisterBuffer, MapAll, UnmapAll, GetMappedPointer），
// 使 ParticleSystemGPU/FluidSystemGPU 可以无缝切换。
//
// 生命周期：必须在 VulkanContext 销毁之前被销毁。
// 线程：所有方法必须从渲染线程调用。

#include "Core/Base.h"
#include "Platform/CUDA/VulkanInteropCommon.h"

#include <cstdint>

// 前向声明 Vulkan 类型（避免包含 vulkan.h）
typedef struct VkDevice_T*         VkDevice;
typedef struct VkPhysicalDevice_T* VkPhysicalDevice;
typedef struct VkQueue_T*          VkQueue;

namespace Engine
{

    class CudaVulkanInteropContext
    {
    public:
        // 探测当前 Vulkan 设备是否支持 CUDA 互操作。
        // 需要设备支持 VK_KHR_external_memory 和 VK_KHR_timeline_semaphore。
        static bool ProbeDeviceMatch(VkPhysicalDevice physicalDevice);

        struct CreateInfo
        {
            VkDevice         Device         = nullptr;
            VkPhysicalDevice PhysicalDevice = nullptr;
            VkQueue          Queue          = nullptr; // 用于同步操作的队列
            uint32_t         QueueFamily    = 0;
        };

        CudaVulkanInteropContext();
        ~CudaVulkanInteropContext();

        // 非可复制，非可移动（资源与设备/上下文相关）
        CudaVulkanInteropContext(const CudaVulkanInteropContext&)            = delete;
        CudaVulkanInteropContext& operator=(const CudaVulkanInteropContext&) = delete;

        // 初始化上下文。必须在 RegisterBuffer 之前调用。
        // 成功返回 true，失败返回 false（调用者应回退到其他路径）。
        bool Initialize(const CreateInfo& info);

        // 为 CUDA 互操作创建并注册 Vulkan 缓冲区。
        // 缓冲区将使用 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT。
        // 内存类型为设备本地 + 可导出。
        // 成功返回槽索引 (>= 0)，失败返回 -1。
        int RegisterBuffer(size_t sizeInBytes, const char* debugName);

        // 注销所有先前注册的缓冲区并释放 Vulkan/CUDA 资源。
        void UnregisterAll();

        // 在 CUDA 访问缓冲区之前调用。
        // 插入 Timeline Semaphore 等待，确保 Vulkan 工作完成。
        // 失败返回 false（调用者应永久回退）。
        bool MapAll();

        // CUDA 工作完成后调用，将缓冲区返回给 Vulkan。
        // 发出 CUDA 信号，使 Vulkan 可以等待。
        void UnmapAll();

        // 已注册槽的 CUDA 设备指针。仅在 MapAll/UnmapAll 之间有效。
        void* GetMappedPointer(int slot) const;

        // 获取槽的 Vulkan 缓冲区句柄（用于绑定到渲染管线）。
        // 返回 VkBuffer，调用者需强制转换。
        void* GetVulkanBuffer(int slot) const;

        // 不透明流句柄——在 .cu 代码中强制转换为 cudaStream_t。
        void* GetStream() const;

        bool IsMapped() const;
        int  GetSlotCount() const;

        // 获取当前帧同步值（用于 Vulkan 端等待/信号）
        CudaInterop::InteropFrameSyncValues GetCurrentSyncValues() const;

        // 推进到下一帧（在帧结束时调用）
        void AdvanceFrame();

    private:
        struct Impl;
        Scope<Impl> m_Impl;
    };

} // namespace Engine
