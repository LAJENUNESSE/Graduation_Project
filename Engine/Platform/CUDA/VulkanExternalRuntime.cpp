// VulkanExternalRuntime 实现
// 条件编译：仅当 ENGINE_ENABLE_VULKAN_CUDA_INTEROP=1 时启用

// 必须在包含任何 Vulkan 头文件之前定义平台宏
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include "Core/VulkanExternalRuntime.h"

#ifdef ENGINE_ENABLE_VULKAN_CUDA_INTEROP

#include "Core/Log.h"
#include "Platform/CUDA/CudaErrorHandling.h"
#include "Platform/CUDA/VulkanExternalInterop.h"
#include "Platform/CUDA/VulkanExternalSmokeKernel.h"
#include "Platform/CUDA/VulkanInteropCommon.h"

#include <chrono>
#include <cstring>
#include <vector>

#include <cuda_runtime.h>
#include <vulkan/vulkan.h>

namespace Engine
{
    // ========================================================================
    // Vulkan 资源结构
    // ========================================================================

    struct VulkanExternalRuntime::VulkanResources
    {
        VkInstance       Instance       = VK_NULL_HANDLE;
        VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
        VkDevice         Device         = VK_NULL_HANDLE;
        VkQueue          Queue          = VK_NULL_HANDLE;
        uint32_t         QueueFamily    = 0;

        // 共享资源
        VkBuffer       SharedBuffer       = VK_NULL_HANDLE;
        VkDeviceMemory SharedBufferMemory = VK_NULL_HANDLE;
        VkSemaphore    TimelineSemaphore  = VK_NULL_HANDLE;

        // 导出句柄
        CudaInterop::OwnedInteropHandle MemoryHandle;
        CudaInterop::OwnedInteropHandle SemaphoreHandle;

        // 命令缓冲
        VkCommandPool   CommandPool   = VK_NULL_HANDLE;
        VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;

        // 设备信息
        uint32_t ApiVersion      = 0;
        uint32_t DeviceId        = 0;
        char     DeviceName[256] = {};
    };

    // ========================================================================
    // CUDA 资源结构
    // ========================================================================

    struct VulkanExternalRuntime::CudaResources
    {
        CudaInterop::CudaExternalInteropContext InteropContext;
        int                                     DeviceIndex = -1;
    };

    // ========================================================================
    // 单例
    // ========================================================================

    VulkanExternalRuntime& VulkanExternalRuntime::Get()
    {
        static VulkanExternalRuntime instance;
        return instance;
    }

    VulkanExternalRuntime::~VulkanExternalRuntime()
    {
        Shutdown();
    }

    // ========================================================================
    // 初始化与关闭
    // ========================================================================

    bool VulkanExternalRuntime::Initialize()
    {
        if (m_Initialized)
        {
            ENGINE_CORE_WARN("[Particle][VkExtSkeleton] Already initialized");
            return m_Ready;
        }

        ENGINE_CORE_INFO("[Particle][VkExtSkeleton] Initializing VulkanExternalRuntime...");
        ENGINE_CORE_INFO("[Particle][VkExtSkeleton] Note: This is skeleton validation only, not main render path");

        // 1. 能力探测
        m_CapabilityReport = ProbeCapability();
        if (!m_CapabilityReport.Supported)
        {
            ENGINE_CORE_WARN("[Particle][VkExtSkeleton] Capability check failed: {}",
                             ToString(m_CapabilityReport.FailReason));
            m_Initialized = true;
            m_Ready       = false;
            m_LastStatus  = VkExtSkeletonStatus::Failed;
            return false;
        }

        ENGINE_CORE_INFO("[Particle][VkExtSkeleton] Capability check passed - Device: {} (Vulkan {}.{}.{})",
                         m_CapabilityReport.DeviceName, VK_API_VERSION_MAJOR(m_CapabilityReport.VulkanApiVersion),
                         VK_API_VERSION_MINOR(m_CapabilityReport.VulkanApiVersion),
                         VK_API_VERSION_PATCH(m_CapabilityReport.VulkanApiVersion));

        // 2. 创建 Vulkan 资源
        if (!CreateVulkanResources())
        {
            ENGINE_CORE_ERROR("[Particle][VkExtSkeleton] Failed to create Vulkan resources");
            m_CapabilityReport.Supported  = false;
            m_CapabilityReport.FailReason = VkExtCapabilityFailReason::ResourceCreationFailed;
            m_Initialized                 = true;
            m_Ready                       = false;
            m_LastStatus                  = VkExtSkeletonStatus::Failed;
            return false;
        }

        // 3. 创建 CUDA 资源
        if (!CreateCudaResources())
        {
            ENGINE_CORE_ERROR("[Particle][VkExtSkeleton] Failed to create CUDA resources");
            DestroyVulkanResources();
            m_CapabilityReport.Supported  = false;
            m_CapabilityReport.FailReason = VkExtCapabilityFailReason::ResourceCreationFailed;
            m_Initialized                 = true;
            m_Ready                       = false;
            m_LastStatus                  = VkExtSkeletonStatus::Failed;
            return false;
        }

        // 4. 执行自检
        m_Initialized = true;
        if (!RunSelfCheck())
        {
            ENGINE_CORE_ERROR("[Particle][VkExtSkeleton] Self-check failed");
            m_CapabilityReport.Supported  = false;
            m_CapabilityReport.FailReason = VkExtCapabilityFailReason::SelfCheckFailed;
            m_Ready                       = false;
            m_LastStatus                  = VkExtSkeletonStatus::Failed;
            return false;
        }

        ENGINE_CORE_INFO("[Particle][VkExtSkeleton] Initialization successful - vkext runtime ready");
        m_Ready      = true;
        m_LastStatus = VkExtSkeletonStatus::Success;
        return true;
    }

    void VulkanExternalRuntime::Shutdown()
    {
        if (!m_Initialized)
            return;

        ENGINE_CORE_INFO("[Particle][VkExtSkeleton] Shutting down VulkanExternalRuntime...");

        DestroyCudaResources();
        DestroyVulkanResources();

        m_Initialized = false;
        m_Ready       = false;
        m_LastStatus  = VkExtSkeletonStatus::NotRun;
    }

    // ========================================================================
    // 能力探测（静态）
    // ========================================================================

    VulkanInteropCapabilityReport VulkanExternalRuntime::ProbeCapability()
    {
        VulkanInteropCapabilityReport report;
        report.Supported = false;

        // 检查 CUDA 可用性
        int         cudaDeviceCount = 0;
        cudaError_t cudaErr         = cudaGetDeviceCount(&cudaDeviceCount);
        if (cudaErr != cudaSuccess || cudaDeviceCount == 0)
        {
            report.FailReason = VkExtCapabilityFailReason::CudaNotAvailable;
            return report;
        }

        // 创建临时 Vulkan instance 用于探测
        VkApplicationInfo appInfo  = {};
        appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName   = "VkExtCapabilityProbe";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName        = "Engine";
        appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion         = VK_API_VERSION_1_2;

        std::vector<const char*> instanceExtensions;
#ifdef _WIN32
        instanceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
        instanceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);
#endif

        VkInstanceCreateInfo instanceInfo    = {};
        instanceInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pApplicationInfo        = &appInfo;
        instanceInfo.enabledExtensionCount   = static_cast<uint32_t>(instanceExtensions.size());
        instanceInfo.ppEnabledExtensionNames = instanceExtensions.data();

        VkInstance instance = VK_NULL_HANDLE;
        if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS)
        {
            report.FailReason = VkExtCapabilityFailReason::VulkanNotAvailable;
            return report;
        }

        // 枚举物理设备
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        if (deviceCount == 0)
        {
            vkDestroyInstance(instance, nullptr);
            report.FailReason = VkExtCapabilityFailReason::NoSuitablePhysicalDevice;
            return report;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

        // 查找支持所有必需扩展的 NVIDIA 设备
        VkPhysicalDevice           selectedDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties deviceProps;

        for (auto device : devices)
        {
            vkGetPhysicalDeviceProperties(device, &deviceProps);

            // 仅支持 NVIDIA GPU
            if (deviceProps.vendorID != 0x10DE) // NVIDIA vendor ID
                continue;

            // 检查设备扩展
            uint32_t extCount = 0;
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
            std::vector<VkExtensionProperties> extensions(extCount);
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, extensions.data());

            bool hasTimeline       = false;
            bool hasExternalMemory = false;
            bool hasExternalSema   = false;
#ifdef _WIN32
            bool hasExternalMemoryWin32 = false;
            bool hasExternalSemaWin32   = false;
#endif

            for (const auto& ext : extensions)
            {
                if (strcmp(ext.extensionName, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) == 0)
                    hasTimeline = true;
                if (strcmp(ext.extensionName, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME) == 0)
                    hasExternalMemory = true;
                if (strcmp(ext.extensionName, VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME) == 0)
                    hasExternalSema = true;
#ifdef _WIN32
                if (strcmp(ext.extensionName, VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME) == 0)
                    hasExternalMemoryWin32 = true;
                if (strcmp(ext.extensionName, VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME) == 0)
                    hasExternalSemaWin32 = true;
#endif
            }

            if (!hasTimeline)
            {
                report.FailReason = VkExtCapabilityFailReason::TimelineSemaphoreNotSupported;
                continue;
            }
#ifdef _WIN32
            if (!hasExternalMemory || !hasExternalMemoryWin32)
#else
            if (!hasExternalMemory)
#endif
            {
                report.FailReason = VkExtCapabilityFailReason::ExternalMemoryNotSupported;
                continue;
            }
#ifdef _WIN32
            if (!hasExternalSema || !hasExternalSemaWin32)
#else
            if (!hasExternalSema)
#endif
            {
                report.FailReason = VkExtCapabilityFailReason::ExternalSemaphoreNotSupported;
                continue;
            }

            // 找到合适设备
            selectedDevice = device;
            break;
        }

        if (selectedDevice == VK_NULL_HANDLE)
        {
            vkDestroyInstance(instance, nullptr);
            if (report.FailReason == VkExtCapabilityFailReason::None)
                report.FailReason = VkExtCapabilityFailReason::NoSuitablePhysicalDevice;
            return report;
        }

        // 获取 CUDA 设备 UUID 并匹配
        cudaDeviceProp cudaProp;
        bool           deviceMatched     = false;
        int            matchedCudaDevice = 0;

        VkPhysicalDeviceIDProperties idProps = {};
        idProps.sType                        = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

        VkPhysicalDeviceProperties2 props2 = {};
        props2.sType                       = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext                       = &idProps;
        vkGetPhysicalDeviceProperties2(selectedDevice, &props2);

        for (int i = 0; i < cudaDeviceCount; ++i)
        {
            cudaGetDeviceProperties(&cudaProp, i);
            if (memcmp(cudaProp.uuid.bytes, idProps.deviceUUID, VK_UUID_SIZE) == 0)
            {
                deviceMatched     = true;
                matchedCudaDevice = i;
                break;
            }
        }

        if (!deviceMatched)
        {
            vkDestroyInstance(instance, nullptr);
            report.FailReason = VkExtCapabilityFailReason::DeviceMismatch;
            return report;
        }

        // 成功
        report.Supported        = true;
        report.FailReason       = VkExtCapabilityFailReason::None;
        report.VulkanApiVersion = props2.properties.apiVersion;
        report.VulkanDeviceId   = props2.properties.deviceID;
        report.CudaDeviceIndex  = matchedCudaDevice;
        strncpy(report.DeviceName, props2.properties.deviceName, sizeof(report.DeviceName) - 1);
        report.DeviceName[sizeof(report.DeviceName) - 1] = '\0';

        vkDestroyInstance(instance, nullptr);
        return report;
    }

    // ========================================================================
    // 诊断
    // ========================================================================

    VkExtSkeletonDiagnostics VulkanExternalRuntime::GetDiagnostics() const
    {
        VkExtSkeletonDiagnostics diag;
        diag.Active       = m_Initialized && m_Ready;
        diag.LastStatus   = m_LastStatus;
        diag.CudaWaitMs   = m_LastCudaWaitMs;
        diag.CudaKernelMs = m_LastCudaKernelMs;
        diag.VkSubmitMs   = m_LastVkSubmitMs;
        return diag;
    }

    // ========================================================================
    // 自检
    // ========================================================================

    bool VulkanExternalRuntime::RunSelfCheck()
    {
        ENGINE_CORE_INFO("[Particle][VkExtSkeleton] Running self-check: {} frames, {}ms timeout", kSelfCheckFrameCount,
                         kSelfCheckTimeoutMs);

        auto startTime = std::chrono::steady_clock::now();

        for (uint32_t frame = 0; frame < kSelfCheckFrameCount; ++frame)
        {
            // 超时检查
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > kSelfCheckTimeoutMs)
            {
                ENGINE_CORE_ERROR("[Particle][VkExtSkeleton] Self-check timeout at frame {}", frame);
                return false;
            }

            if (!ExecuteSkeletonFrame(frame))
            {
                ENGINE_CORE_ERROR("[Particle][VkExtSkeleton] Self-check failed at frame {}", frame);
                return false;
            }
        }

        auto  elapsed   = std::chrono::steady_clock::now() - startTime;
        float elapsedMs = std::chrono::duration<float, std::milli>(elapsed).count();
        ENGINE_CORE_INFO("[Particle][VkExtSkeleton] Self-check completed: {} frames in {:.2f}ms", kSelfCheckFrameCount,
                         elapsedMs);

        return true;
    }

    // ========================================================================
    // 帧执行
    // ========================================================================

    bool VulkanExternalRuntime::ExecuteSkeletonFrame(uint64_t frameIndex)
    {
        if (!m_Initialized || !m_VkResources || !m_CudaResources)
            return false;

        auto syncValues = CudaInterop::BuildInteropFrameSyncValues(frameIndex);

        // 计时
        auto t0 = std::chrono::steady_clock::now();

        // 1. CUDA wait 上一帧 Vulkan 信号值
        m_CudaResources->InteropContext.WaitTimelineValue(syncValues.WaitVulkanValue);

        auto t1 = std::chrono::steady_clock::now();

        // 2. 启动 smoke kernel
        CudaInterop::LaunchVulkanExternalSmokeKernel(m_CudaResources->InteropContext.GetMappedDevicePointer(),
                                                     kSharedBufferSize, frameIndex,
                                                     m_CudaResources->InteropContext.GetStream());

        m_CudaResources->InteropContext.SyncStream();

        auto t2 = std::chrono::steady_clock::now();

        // 3. CUDA signal 当前帧 CUDA 值
        m_CudaResources->InteropContext.SignalTimelineValue(syncValues.CudaSignalValue);

        auto t3 = std::chrono::steady_clock::now();

        // 4. Vulkan submit 等待 CUDA 值并 signal 当前帧 Vulkan 值
        VkTimelineSemaphoreSubmitInfo timelineInfo = {};
        timelineInfo.sType                         = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        timelineInfo.waitSemaphoreValueCount       = 1;
        timelineInfo.pWaitSemaphoreValues          = &syncValues.CudaSignalValue;
        timelineInfo.signalSemaphoreValueCount     = 1;
        timelineInfo.pSignalSemaphoreValues        = &syncValues.VulkanSignalValue;

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

        VkSubmitInfo submitInfo         = {};
        submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pNext                = &timelineInfo;
        submitInfo.waitSemaphoreCount   = 1;
        submitInfo.pWaitSemaphores      = &m_VkResources->TimelineSemaphore;
        submitInfo.pWaitDstStageMask    = &waitStage;
        submitInfo.commandBufferCount   = 1;
        submitInfo.pCommandBuffers      = &m_VkResources->CommandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores    = &m_VkResources->TimelineSemaphore;

        VkResult result = vkQueueSubmit(m_VkResources->Queue, 1, &submitInfo, VK_NULL_HANDLE);
        if (result != VK_SUCCESS)
        {
            ENGINE_CORE_ERROR("[Particle][VkExtSkeleton] vkQueueSubmit failed: {}", static_cast<int>(result));
            return false;
        }

        auto t4 = std::chrono::steady_clock::now();

        // 更新诊断计时
        m_LastCudaWaitMs   = std::chrono::duration<float, std::milli>(t1 - t0).count();
        m_LastCudaKernelMs = std::chrono::duration<float, std::milli>(t2 - t1).count();
        m_LastVkSubmitMs   = std::chrono::duration<float, std::milli>(t4 - t3).count();

        return true;
    }

    // ========================================================================
    // Vulkan 资源管理
    // ========================================================================

    bool VulkanExternalRuntime::CreateVulkanResources()
    {
        m_VkResources = new VulkanResources();

        // 创建 Instance
        VkApplicationInfo appInfo  = {};
        appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName   = "VkExtSkeleton";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName        = "Engine";
        appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion         = VK_API_VERSION_1_2;

        std::vector<const char*> instanceExtensions;
#ifdef _WIN32
        instanceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
        instanceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);
#endif

        VkInstanceCreateInfo instanceInfo    = {};
        instanceInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pApplicationInfo        = &appInfo;
        instanceInfo.enabledExtensionCount   = static_cast<uint32_t>(instanceExtensions.size());
        instanceInfo.ppEnabledExtensionNames = instanceExtensions.data();

        if (vkCreateInstance(&instanceInfo, nullptr, &m_VkResources->Instance) != VK_SUCCESS)
        {
            ENGINE_CORE_ERROR("[Particle][VkExtSkeleton] Failed to create Vulkan instance");
            return false;
        }

        // 枚举并选择物理设备
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_VkResources->Instance, &deviceCount, nullptr);
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(m_VkResources->Instance, &deviceCount, devices.data());

        // 使用能力报告中的 CUDA 设备 UUID 匹配 Vulkan 物理设备
        cudaDeviceProp cudaProp;
        cudaGetDeviceProperties(&cudaProp, m_CapabilityReport.CudaDeviceIndex);

        for (auto device : devices)
        {
            VkPhysicalDeviceIDProperties idProps = {};
            idProps.sType                        = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

            VkPhysicalDeviceProperties2 props2 = {};
            props2.sType                       = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            props2.pNext                       = &idProps;
            vkGetPhysicalDeviceProperties2(device, &props2);

            if (memcmp(cudaProp.uuid.bytes, idProps.deviceUUID, VK_UUID_SIZE) == 0)
            {
                m_VkResources->PhysicalDevice = device;
                m_VkResources->ApiVersion     = props2.properties.apiVersion;
                m_VkResources->DeviceId       = props2.properties.deviceID;
                strncpy(m_VkResources->DeviceName, props2.properties.deviceName, sizeof(m_VkResources->DeviceName) - 1);
                break;
            }
        }

        if (m_VkResources->PhysicalDevice == VK_NULL_HANDLE)
        {
            ENGINE_CORE_ERROR("[Particle][VkExtSkeleton] Failed to find matching Vulkan physical device");
            return false;
        }

        // 查找队列族
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_VkResources->PhysicalDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(m_VkResources->PhysicalDevice, &queueFamilyCount,
                                                 queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; ++i)
        {
            if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
            {
                m_VkResources->QueueFamily = i;
                break;
            }
        }

        // 创建逻辑设备
        float                   queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo     = {};
        queueInfo.sType                       = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex            = m_VkResources->QueueFamily;
        queueInfo.queueCount                  = 1;
        queueInfo.pQueuePriorities            = &queuePriority;

        std::vector<const char*> deviceExtensions = {
            VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,    VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
#ifdef _WIN32
            VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
#else
            VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
#endif
        };

        VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeatures = {};
        timelineFeatures.sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
        timelineFeatures.timelineSemaphore = VK_TRUE;

        VkDeviceCreateInfo deviceInfo      = {};
        deviceInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.pNext                   = &timelineFeatures;
        deviceInfo.queueCreateInfoCount    = 1;
        deviceInfo.pQueueCreateInfos       = &queueInfo;
        deviceInfo.enabledExtensionCount   = static_cast<uint32_t>(deviceExtensions.size());
        deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();

        if (vkCreateDevice(m_VkResources->PhysicalDevice, &deviceInfo, nullptr, &m_VkResources->Device) != VK_SUCCESS)
        {
            ENGINE_CORE_ERROR("[Particle][VkExtSkeleton] Failed to create Vulkan device");
            return false;
        }

        vkGetDeviceQueue(m_VkResources->Device, m_VkResources->QueueFamily, 0, &m_VkResources->Queue);

        // 创建共享 buffer
        VkExternalMemoryBufferCreateInfo externalMemBufferInfo = {};
        externalMemBufferInfo.sType                            = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
        externalMemBufferInfo.handleTypes = CudaInterop::VulkanExternalExport::VkMemoryHandleType();

        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.pNext              = &externalMemBufferInfo;
        bufferInfo.size               = kSharedBufferSize;
        bufferInfo.usage              = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_VkResources->Device, &bufferInfo, nullptr, &m_VkResources->SharedBuffer) != VK_SUCCESS)
        {
            ENGINE_CORE_ERROR("[Particle][VkExtSkeleton] Failed to create shared buffer");
            return false;
        }

        // 分配内存
        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(m_VkResources->Device, m_VkResources->SharedBuffer, &memReqs);

        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(m_VkResources->PhysicalDevice, &memProps);

        uint32_t memTypeIndex = UINT32_MAX;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        {
            if ((memReqs.memoryTypeBits & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            {
                memTypeIndex = i;
                break;
            }
        }

        if (memTypeIndex == UINT32_MAX)
        {
            ENGINE_CORE_ERROR("[Particle][VkExtSkeleton] No suitable memory type found");
            return false;
        }

        VkExportMemoryAllocateInfo exportMemInfo = {};
        exportMemInfo.sType                      = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
        exportMemInfo.handleTypes                = CudaInterop::VulkanExternalExport::VkMemoryHandleType();

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext                = &exportMemInfo;
        allocInfo.allocationSize       = memReqs.size;
        allocInfo.memoryTypeIndex      = memTypeIndex;

        if (vkAllocateMemory(m_VkResources->Device, &allocInfo, nullptr, &m_VkResources->SharedBufferMemory) !=
            VK_SUCCESS)
        {
            ENGINE_CORE_ERROR("[Particle][VkExtSkeleton] Failed to allocate shared buffer memory");
            return false;
        }

        vkBindBufferMemory(m_VkResources->Device, m_VkResources->SharedBuffer, m_VkResources->SharedBufferMemory, 0);

        // 创建 timeline semaphore
        VkSemaphoreTypeCreateInfo semTypeInfo = {};
        semTypeInfo.sType                     = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        semTypeInfo.semaphoreType             = VK_SEMAPHORE_TYPE_TIMELINE;
        semTypeInfo.initialValue              = 0;

        VkExportSemaphoreCreateInfo exportSemInfo = {};
        exportSemInfo.sType                       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
        exportSemInfo.pNext                       = &semTypeInfo;
        exportSemInfo.handleTypes                 = CudaInterop::VulkanExternalExport::VkSemaphoreHandleType();

        VkSemaphoreCreateInfo semInfo = {};
        semInfo.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semInfo.pNext                 = &exportSemInfo;

        if (vkCreateSemaphore(m_VkResources->Device, &semInfo, nullptr, &m_VkResources->TimelineSemaphore) !=
            VK_SUCCESS)
        {
            ENGINE_CORE_ERROR("[Particle][VkExtSkeleton] Failed to create timeline semaphore");
            return false;
        }

        // 导出句柄
        m_VkResources->MemoryHandle =
            CudaInterop::VulkanExternalExport::ExportMemory(m_VkResources->Device, m_VkResources->SharedBufferMemory);
        m_VkResources->SemaphoreHandle = CudaInterop::VulkanExternalExport::ExportTimelineSemaphore(
            m_VkResources->Device, m_VkResources->TimelineSemaphore);

        if (!m_VkResources->MemoryHandle.IsValid() || !m_VkResources->SemaphoreHandle.IsValid())
        {
            ENGINE_CORE_ERROR("[Particle][VkExtSkeleton] Failed to export Vulkan handles");
            return false;
        }

        // 创建命令池和命令缓冲（用于空提交）
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex        = m_VkResources->QueueFamily;
        poolInfo.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(m_VkResources->Device, &poolInfo, nullptr, &m_VkResources->CommandPool) != VK_SUCCESS)
        {
            ENGINE_CORE_ERROR("[Particle][VkExtSkeleton] Failed to create command pool");
            return false;
        }

        VkCommandBufferAllocateInfo cmdAllocInfo = {};
        cmdAllocInfo.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.commandPool                 = m_VkResources->CommandPool;
        cmdAllocInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAllocInfo.commandBufferCount          = 1;

        if (vkAllocateCommandBuffers(m_VkResources->Device, &cmdAllocInfo, &m_VkResources->CommandBuffer) != VK_SUCCESS)
        {
            ENGINE_CORE_ERROR("[Particle][VkExtSkeleton] Failed to allocate command buffer");
            return false;
        }

        // 录制空命令缓冲
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(m_VkResources->CommandBuffer, &beginInfo);
        vkEndCommandBuffer(m_VkResources->CommandBuffer);

        ENGINE_CORE_INFO("[Particle][VkExtSkeleton] Vulkan resources created successfully");
        return true;
    }

    void VulkanExternalRuntime::DestroyVulkanResources()
    {
        if (!m_VkResources)
            return;

        if (m_VkResources->Device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(m_VkResources->Device);

            if (m_VkResources->CommandPool != VK_NULL_HANDLE)
                vkDestroyCommandPool(m_VkResources->Device, m_VkResources->CommandPool, nullptr);
            if (m_VkResources->TimelineSemaphore != VK_NULL_HANDLE)
                vkDestroySemaphore(m_VkResources->Device, m_VkResources->TimelineSemaphore, nullptr);
            if (m_VkResources->SharedBuffer != VK_NULL_HANDLE)
                vkDestroyBuffer(m_VkResources->Device, m_VkResources->SharedBuffer, nullptr);
            if (m_VkResources->SharedBufferMemory != VK_NULL_HANDLE)
                vkFreeMemory(m_VkResources->Device, m_VkResources->SharedBufferMemory, nullptr);

            vkDestroyDevice(m_VkResources->Device, nullptr);
        }

        if (m_VkResources->Instance != VK_NULL_HANDLE)
            vkDestroyInstance(m_VkResources->Instance, nullptr);

        delete m_VkResources;
        m_VkResources = nullptr;
    }

    // ========================================================================
    // CUDA 资源管理
    // ========================================================================

    bool VulkanExternalRuntime::CreateCudaResources()
    {
        if (!m_VkResources)
            return false;

        m_CudaResources              = new CudaResources();
        m_CudaResources->DeviceIndex = m_CapabilityReport.CudaDeviceIndex;

        CudaInterop::CudaExternalInteropContext::InitDesc initDesc;
        initDesc.MemoryHandle     = m_VkResources->MemoryHandle;
        initDesc.SemaphoreHandle  = m_VkResources->SemaphoreHandle;
        initDesc.SharedBufferSize = kSharedBufferSize;
        initDesc.CudaDeviceIndex  = m_CudaResources->DeviceIndex;
        initDesc.HandleType       = CudaInterop::GetDefaultExternalHandleType();

        try
        {
            m_CudaResources->InteropContext.Initialize(initDesc);
        }
        catch (const std::exception& e)
        {
            ENGINE_CORE_ERROR("[Particle][VkExtSkeleton] Failed to initialize CUDA interop context: {}", e.what());
            delete m_CudaResources;
            m_CudaResources = nullptr;
            return false;
        }

        // 标记句柄已被 CUDA 导入
        m_VkResources->MemoryHandle.MarkImportedByCuda();
        m_VkResources->SemaphoreHandle.MarkImportedByCuda();

        ENGINE_CORE_INFO("[Particle][VkExtSkeleton] CUDA resources created successfully (device {})",
                         m_CudaResources->DeviceIndex);
        return true;
    }

    void VulkanExternalRuntime::DestroyCudaResources()
    {
        if (!m_CudaResources)
            return;

        m_CudaResources->InteropContext.Destroy();
        delete m_CudaResources;
        m_CudaResources = nullptr;
    }

} // namespace Engine

#else // !ENGINE_ENABLE_VULKAN_CUDA_INTEROP

// ============================================================================
// Stub 实现（未启用 Vulkan-CUDA interop 时）
// ============================================================================

namespace Engine
{
    VulkanExternalRuntime& VulkanExternalRuntime::Get()
    {
        static VulkanExternalRuntime instance;
        return instance;
    }

    VulkanExternalRuntime::~VulkanExternalRuntime() {}

    bool VulkanExternalRuntime::Initialize()
    {
        m_CapabilityReport.Supported  = false;
        m_CapabilityReport.FailReason = VkExtCapabilityFailReason::VulkanNotAvailable;
        return false;
    }

    void VulkanExternalRuntime::Shutdown() {}

    VulkanInteropCapabilityReport VulkanExternalRuntime::ProbeCapability()
    {
        VulkanInteropCapabilityReport report;
        report.Supported  = false;
        report.FailReason = VkExtCapabilityFailReason::VulkanNotAvailable;
        return report;
    }

    VkExtSkeletonDiagnostics VulkanExternalRuntime::GetDiagnostics() const
    {
        return VkExtSkeletonDiagnostics{};
    }

    bool VulkanExternalRuntime::ExecuteSkeletonFrame(uint64_t)
    {
        return false;
    }
    bool VulkanExternalRuntime::RunSelfCheck()
    {
        return false;
    }
    bool VulkanExternalRuntime::CreateVulkanResources()
    {
        return false;
    }
    void VulkanExternalRuntime::DestroyVulkanResources() {}
    bool VulkanExternalRuntime::CreateCudaResources()
    {
        return false;
    }
    void VulkanExternalRuntime::DestroyCudaResources() {}

} // namespace Engine

#endif // ENGINE_ENABLE_VULKAN_CUDA_INTEROP
