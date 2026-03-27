#pragma once

// VulkanExternalRuntime - Engine 内可复用的 Vulkan-CUDA interop 运行时
//
// 功能：
// 1. Vulkan 能力探测（timeline semaphore、external memory/semaphore）
// 2. headless 逻辑设备创建（无 swapchain/present）
// 3. external buffer + timeline semaphore 创建与导出
// 4. CUDA 导入 + 每帧 wait/signal + Vulkan submit/signal 闭环
// 5. Smoke kernel 自检（30 帧 + 2000ms 超时）
//
// 本期定位：骨架验证，不承担主渲染输出

#include <cstdint>

namespace Engine
{
    /// 能力报告失败原因枚举
    enum class VkExtCapabilityFailReason : uint8_t
    {
        None = 0,                      ///< 无失败（supported = true）
        VulkanNotAvailable,            ///< Vulkan SDK/loader 不可用
        NoSuitablePhysicalDevice,      ///< 无合适 GPU
        TimelineSemaphoreNotSupported, ///< 不支持 VK_KHR_timeline_semaphore
        ExternalMemoryNotSupported,    ///< 不支持 VK_KHR_external_memory
        ExternalSemaphoreNotSupported, ///< 不支持 VK_KHR_external_semaphore
        DeviceMismatch,                ///< Vulkan 与 CUDA 设备不匹配
        CudaNotAvailable,              ///< CUDA 不可用
        ResourceCreationFailed,        ///< 资源创建失败（buffer/semaphore）
        SelfCheckFailed,               ///< 自检帧执行失败
        Unknown                        ///< 未知错误
    };

    /// 将失败原因转为字符串（用于日志）
    inline constexpr const char* ToString(VkExtCapabilityFailReason reason)
    {
        switch (reason)
        {
        case VkExtCapabilityFailReason::None:
            return "None";
        case VkExtCapabilityFailReason::VulkanNotAvailable:
            return "VulkanNotAvailable";
        case VkExtCapabilityFailReason::NoSuitablePhysicalDevice:
            return "NoSuitablePhysicalDevice";
        case VkExtCapabilityFailReason::TimelineSemaphoreNotSupported:
            return "TimelineSemaphoreNotSupported";
        case VkExtCapabilityFailReason::ExternalMemoryNotSupported:
            return "ExternalMemoryNotSupported";
        case VkExtCapabilityFailReason::ExternalSemaphoreNotSupported:
            return "ExternalSemaphoreNotSupported";
        case VkExtCapabilityFailReason::DeviceMismatch:
            return "DeviceMismatch";
        case VkExtCapabilityFailReason::CudaNotAvailable:
            return "CudaNotAvailable";
        case VkExtCapabilityFailReason::ResourceCreationFailed:
            return "ResourceCreationFailed";
        case VkExtCapabilityFailReason::SelfCheckFailed:
            return "SelfCheckFailed";
        case VkExtCapabilityFailReason::Unknown:
        default:
            return "Unknown";
        }
    }

    /// 能力探测报告
    struct VulkanInteropCapabilityReport
    {
        bool                      Supported  = false; ///< 是否支持 vkext interop
        VkExtCapabilityFailReason FailReason = VkExtCapabilityFailReason::None;

        /// 设备信息（仅 Supported=true 时有效）
        uint32_t VulkanApiVersion = 0;
        uint32_t VulkanDeviceId   = 0;
        int      CudaDeviceIndex  = -1;
        char     DeviceName[256]  = {};
    };

    /// 骨架运行状态枚举
    enum class VkExtSkeletonStatus : uint8_t
    {
        NotRun  = 0, ///< 尚未运行
        Success = 1, ///< 自检成功
        Failed  = 2  ///< 自检失败
    };

    /// 骨架帧诊断数据（用于 PerformanceMonitor）
    struct VkExtSkeletonDiagnostics
    {
        bool                Active       = false; ///< 骨架是否活跃
        VkExtSkeletonStatus LastStatus   = VkExtSkeletonStatus::NotRun;
        float               CudaWaitMs   = 0.0f; ///< CUDA wait timeline 耗时
        float               CudaKernelMs = 0.0f; ///< Smoke kernel 耗时
        float               VkSubmitMs   = 0.0f; ///< Vulkan submit 耗时
    };

    /// VulkanExternalRuntime 单例
    /// 在 Application::InitSubsystems() 中初始化
    class VulkanExternalRuntime
    {
    public:
        /// 获取单例
        static VulkanExternalRuntime& Get();

        /// 初始化运行时（执行能力探测 + 自检）
        /// @return true 如果初始化成功且自检通过
        bool Initialize();

        /// 关闭运行时，释放所有资源
        void Shutdown();

        /// 获取能力报告（静态查询，不依赖实例状态）
        static VulkanInteropCapabilityReport ProbeCapability();

        /// 获取当前实例的能力报告（初始化后有效）
        const VulkanInteropCapabilityReport& GetCapabilityReport() const { return m_CapabilityReport; }

        /// 检查是否就绪（初始化成功且自检通过）
        bool IsReady() const { return m_Ready; }

        /// 获取诊断数据（用于 PerformanceMonitor）
        VkExtSkeletonDiagnostics GetDiagnostics() const;

        /// 执行单帧骨架循环（用于持续诊断，非主渲染路径）
        /// @param frameIndex 帧索引
        /// @return true 如果帧执行成功
        bool ExecuteSkeletonFrame(uint64_t frameIndex);

    private:
        VulkanExternalRuntime() = default;
        ~VulkanExternalRuntime();

        VulkanExternalRuntime(const VulkanExternalRuntime&)            = delete;
        VulkanExternalRuntime& operator=(const VulkanExternalRuntime&) = delete;

        /// 执行自检（30 帧 + 2000ms 超时）
        bool RunSelfCheck();

        /// 创建 Vulkan 资源（instance、device、buffer、semaphore）
        bool CreateVulkanResources();

        /// 销毁 Vulkan 资源
        void DestroyVulkanResources();

        /// 创建 CUDA 资源（导入 Vulkan 导出的句柄）
        bool CreateCudaResources();

        /// 销毁 CUDA 资源
        void DestroyCudaResources();

    private:
        bool                          m_Initialized = false;
        bool                          m_Ready       = false;
        VulkanInteropCapabilityReport m_CapabilityReport;
        VkExtSkeletonStatus           m_LastStatus = VkExtSkeletonStatus::NotRun;

        // 诊断计时（最近一帧）
        float m_LastCudaWaitMs   = 0.0f;
        float m_LastCudaKernelMs = 0.0f;
        float m_LastVkSubmitMs   = 0.0f;

        // 自检配置
        static constexpr uint32_t kSelfCheckFrameCount = 30;
        static constexpr uint32_t kSelfCheckTimeoutMs  = 2000;
        static constexpr size_t   kSharedBufferSize    = 64 * 1024; // 64KB smoke buffer

        // Vulkan 资源（前向声明，实现中定义）
        struct VulkanResources;
        VulkanResources* m_VkResources = nullptr;

        // CUDA 资源（前向声明，复用 CudaExternalInteropContext）
        struct CudaResources;
        CudaResources* m_CudaResources = nullptr;
    };

} // namespace Engine
