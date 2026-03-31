#include "engpch.h"
#include "Platform/CUDA/CudaVulkanInteropContext.h"
#include "Platform/CUDA/VulkanExternalInterop.h"
#include "Platform/CUDA/CudaErrorHandling.h"
#include "Core/Log.h"

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#include <windows.h>
#endif
#include <vulkan/vulkan.h>
#include <cuda_runtime.h>

#include <vector>
#include <string>

namespace Engine
{

    // ------------------------------------------------------------------ 辅助函数

    static const char* CudaErrStr(cudaError_t err)
    {
        return cudaGetErrorString(err);
    }

#define CUDA_LOG_ERR(call, err) ENGINE_CORE_ERROR("[CUDA-VK] {0} failed: {1}", #call, CudaErrStr(err))
#define VK_CHECK(result, msg)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((result) != VK_SUCCESS)                                                                                    \
        {                                                                                                              \
            ENGINE_CORE_ERROR("[CUDA-VK] {0} (VkResult={1})", msg, static_cast<int>(result));                          \
            return false;                                                                                              \
        }                                                                                                              \
    } while (0)

    // ------------------------------------------------------------------ 槽位结构

    struct InteropSlot
    {
        VkBuffer                                Buffer = VK_NULL_HANDLE;
        VkDeviceMemory                          Memory = VK_NULL_HANDLE;
        size_t                                  Size   = 0;
        std::string                             Name;
        CudaInterop::CudaExternalInteropContext CudaCtx;
        bool                                    Registered = false;
    };

    // ------------------------------------------------------------------ 实现

    struct CudaVulkanInteropContext::Impl
    {
        VkDevice         Device         = VK_NULL_HANDLE;
        VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
        VkQueue          Queue          = VK_NULL_HANDLE;
        uint32_t         QueueFamily    = 0;

        VkSemaphore TimelineSemaphore = VK_NULL_HANDLE;
        uint64_t    FrameIndex        = 0;
        bool        Mapped            = false;
        bool        Initialized       = false;
        int         CudaDeviceIndex   = 0;

        std::vector<InteropSlot> Slots;
        cudaStream_t             SharedStream = nullptr;

        CudaInterop::ExternalHandleType HandleType = CudaInterop::GetDefaultExternalHandleType();

        bool     CreateTimelineSemaphore();
        void     DestroyTimelineSemaphore();
        bool     FindCudaDevice();
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    };

    bool CudaVulkanInteropContext::Impl::CreateTimelineSemaphore()
    {
        VkSemaphoreTypeCreateInfo typeInfo{};
        typeInfo.sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        typeInfo.initialValue  = 0;

        VkExportSemaphoreCreateInfo exportInfo{};
        exportInfo.sType       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
        exportInfo.pNext       = &typeInfo;
        exportInfo.handleTypes = CudaInterop::VulkanExternalExport::VkSemaphoreHandleType(HandleType);

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semInfo.pNext = &exportInfo;

        VkResult result = vkCreateSemaphore(Device, &semInfo, nullptr, &TimelineSemaphore);
        VK_CHECK(result, "vkCreateSemaphore (timeline)");

        ENGINE_CORE_INFO("[CUDA-VK] Timeline semaphore created");
        return true;
    }

    void CudaVulkanInteropContext::Impl::DestroyTimelineSemaphore()
    {
        if (TimelineSemaphore != VK_NULL_HANDLE && Device != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(Device, TimelineSemaphore, nullptr);
            TimelineSemaphore = VK_NULL_HANDLE;
        }
    }

    bool CudaVulkanInteropContext::Impl::FindCudaDevice()
    {
        int         deviceCount = 0;
        cudaError_t err         = cudaGetDeviceCount(&deviceCount);
        if (err != cudaSuccess || deviceCount == 0)
        {
            ENGINE_CORE_ERROR("[CUDA-VK] No CUDA devices found");
            return false;
        }

        // 获取 Vulkan 物理设备的 UUID
        VkPhysicalDeviceIDProperties idProps{};
        idProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &idProps;
        vkGetPhysicalDeviceProperties2(PhysicalDevice, &props2);

        // 在 CUDA 设备中查找匹配的 UUID
        for (int i = 0; i < deviceCount; ++i)
        {
            cudaDeviceProp cudaProps{};
            cudaGetDeviceProperties(&cudaProps, i);

            // 比较 UUID (16 bytes)
            if (std::memcmp(cudaProps.uuid.bytes, idProps.deviceUUID, VK_UUID_SIZE) == 0)
            {
                CudaDeviceIndex = i;
                ENGINE_CORE_INFO("[CUDA-VK] Matched CUDA device {0}: {1} (SM {2}.{3})", i, cudaProps.name,
                                 cudaProps.major, cudaProps.minor);
                return true;
            }
        }

        ENGINE_CORE_ERROR("[CUDA-VK] No CUDA device matches Vulkan physical device UUID");
        return false;
    }

    uint32_t CudaVulkanInteropContext::Impl::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &memProps);

        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        {
            if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
                return i;
        }
        return UINT32_MAX;
    }

    // ------------------------------------------------------------------ 静态方法

    bool CudaVulkanInteropContext::ProbeDeviceMatch(VkPhysicalDevice physicalDevice)
    {
        if (CudaInterop::IsCudaPoisoned())
            return false;

        if (!physicalDevice)
            return false;

        // 检查必需的扩展支持
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);

        std::vector<VkExtensionProperties> extensions(extCount);
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, extensions.data());

        bool hasExternalMemory    = false;
        bool hasExternalSemaphore = false;
        bool hasTimelineSemaphore = false;

        for (const auto& ext : extensions)
        {
            if (std::strcmp(ext.extensionName, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME) == 0)
                hasExternalMemory = true;
            if (std::strcmp(ext.extensionName, VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME) == 0)
                hasExternalSemaphore = true;
            if (std::strcmp(ext.extensionName, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) == 0)
                hasTimelineSemaphore = true;
        }

        if (!hasExternalMemory || !hasExternalSemaphore || !hasTimelineSemaphore)
        {
            ENGINE_CORE_WARN("[CUDA-VK] ProbeDeviceMatch: missing required extensions "
                             "(extMem={0}, extSem={1}, timeline={2})",
                             hasExternalMemory, hasExternalSemaphore, hasTimelineSemaphore);
            return false;
        }

        // 验证有匹配的 CUDA 设备
        VkPhysicalDeviceIDProperties idProps{};
        idProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &idProps;
        vkGetPhysicalDeviceProperties2(physicalDevice, &props2);

        int deviceCount = 0;
        cudaGetDeviceCount(&deviceCount);
        for (int i = 0; i < deviceCount; ++i)
        {
            cudaDeviceProp cudaProps{};
            cudaGetDeviceProperties(&cudaProps, i);
            if (std::memcmp(cudaProps.uuid.bytes, idProps.deviceUUID, VK_UUID_SIZE) == 0)
            {
                ENGINE_CORE_INFO("[CUDA-VK] ProbeDeviceMatch: found matching CUDA device {0}", i);
                return true;
            }
        }

        ENGINE_CORE_WARN("[CUDA-VK] ProbeDeviceMatch: no CUDA device matches Vulkan UUID");
        return false;
    }

    // ------------------------------------------------------------------ 构造/析构

    CudaVulkanInteropContext::CudaVulkanInteropContext() : m_Impl(CreateScope<Impl>()) {}

    CudaVulkanInteropContext::~CudaVulkanInteropContext()
    {
        if (m_Impl->Mapped)
            UnmapAll();
        UnregisterAll();
        m_Impl->DestroyTimelineSemaphore();

        if (m_Impl->SharedStream)
        {
            cudaStreamDestroy(m_Impl->SharedStream);
            m_Impl->SharedStream = nullptr;
        }
    }

    // ------------------------------------------------------------------ 初始化

    bool CudaVulkanInteropContext::Initialize(const CreateInfo& info)
    {
        if (m_Impl->Initialized)
        {
            ENGINE_CORE_ERROR("[CUDA-VK] Already initialized");
            return false;
        }

        if (!info.Device || !info.PhysicalDevice)
        {
            ENGINE_CORE_ERROR("[CUDA-VK] Invalid Vulkan device handles");
            return false;
        }

        m_Impl->Device         = info.Device;
        m_Impl->PhysicalDevice = info.PhysicalDevice;
        m_Impl->Queue          = info.Queue;
        m_Impl->QueueFamily    = info.QueueFamily;

        // 查找匹配的 CUDA 设备
        if (!m_Impl->FindCudaDevice())
        {
            CudaInterop::PoisonCuda("No CUDA device matching Vulkan device");
            return false;
        }

        // 设置 CUDA 设备
        if (!CUDA_CHECK(cudaSetDevice(m_Impl->CudaDeviceIndex)))
            return false;

        // 创建共享 CUDA 流
        if (!CUDA_CHECK(cudaStreamCreate(&m_Impl->SharedStream)))
            return false;

        // 创建 Timeline Semaphore
        if (!m_Impl->CreateTimelineSemaphore())
            return false;

        m_Impl->Initialized = true;
        ENGINE_CORE_INFO("[CUDA-VK] CudaVulkanInteropContext initialized successfully");
        return true;
    }

    // ------------------------------------------------------------------ 缓冲区注册

    int CudaVulkanInteropContext::RegisterBuffer(size_t sizeInBytes, const char* debugName)
    {
        if (!m_Impl->Initialized)
        {
            ENGINE_CORE_ERROR("[CUDA-VK] RegisterBuffer: not initialized");
            return -1;
        }

        if (sizeInBytes == 0)
        {
            ENGINE_CORE_ERROR("[CUDA-VK] RegisterBuffer: size must be > 0");
            return -1;
        }

        InteropSlot slot{};
        slot.Size = sizeInBytes;
        slot.Name = debugName ? debugName : "";

        // 创建带外部内存导出标志的 Vulkan 缓冲区
        VkExternalMemoryBufferCreateInfo externalInfo{};
        externalInfo.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
        externalInfo.handleTypes = CudaInterop::VulkanExternalExport::VkMemoryHandleType(m_Impl->HandleType);

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.pNext = &externalInfo;
        bufferInfo.size  = sizeInBytes;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult result = vkCreateBuffer(m_Impl->Device, &bufferInfo, nullptr, &slot.Buffer);
        if (result != VK_SUCCESS)
        {
            ENGINE_CORE_ERROR("[CUDA-VK] RegisterBuffer '{0}': vkCreateBuffer failed (VkResult={1})", slot.Name,
                              static_cast<int>(result));
            return -1;
        }

        // 获取内存需求
        VkMemoryRequirements memReqs{};
        vkGetBufferMemoryRequirements(m_Impl->Device, slot.Buffer, &memReqs);

        // 分配带导出标志的设备内存
        VkExportMemoryAllocateInfo exportMemInfo{};
        exportMemInfo.sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
        exportMemInfo.handleTypes = CudaInterop::VulkanExternalExport::VkMemoryHandleType(m_Impl->HandleType);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext           = &exportMemInfo;
        allocInfo.allocationSize  = memReqs.size;
        allocInfo.memoryTypeIndex = m_Impl->FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (allocInfo.memoryTypeIndex == UINT32_MAX)
        {
            ENGINE_CORE_ERROR("[CUDA-VK] RegisterBuffer '{0}': no suitable memory type", slot.Name);
            vkDestroyBuffer(m_Impl->Device, slot.Buffer, nullptr);
            return -1;
        }

        result = vkAllocateMemory(m_Impl->Device, &allocInfo, nullptr, &slot.Memory);
        if (result != VK_SUCCESS)
        {
            ENGINE_CORE_ERROR("[CUDA-VK] RegisterBuffer '{0}': vkAllocateMemory failed (VkResult={1})", slot.Name,
                              static_cast<int>(result));
            vkDestroyBuffer(m_Impl->Device, slot.Buffer, nullptr);
            return -1;
        }

        result = vkBindBufferMemory(m_Impl->Device, slot.Buffer, slot.Memory, 0);
        if (result != VK_SUCCESS)
        {
            ENGINE_CORE_ERROR("[CUDA-VK] RegisterBuffer '{0}': vkBindBufferMemory failed", slot.Name);
            vkFreeMemory(m_Impl->Device, slot.Memory, nullptr);
            vkDestroyBuffer(m_Impl->Device, slot.Buffer, nullptr);
            return -1;
        }

        // 导出内存和信号量句柄
        try
        {
            CudaInterop::OwnedInteropHandle memHandle =
                CudaInterop::VulkanExternalExport::ExportMemory(m_Impl->Device, slot.Memory, m_Impl->HandleType);

            CudaInterop::OwnedInteropHandle semHandle = CudaInterop::VulkanExternalExport::ExportTimelineSemaphore(
                m_Impl->Device, m_Impl->TimelineSemaphore, m_Impl->HandleType);

            // 初始化 CUDA 导入上下文
            CudaInterop::CudaExternalInteropContext::InitDesc initDesc{};
            initDesc.MemoryHandle     = memHandle;
            initDesc.SemaphoreHandle  = semHandle;
            initDesc.SharedBufferSize = memReqs.size;
            initDesc.CudaDeviceIndex  = m_Impl->CudaDeviceIndex;
            initDesc.HandleType       = m_Impl->HandleType;

            slot.CudaCtx.Initialize(initDesc);
            slot.Registered = true;
        }
        catch (const std::exception& e)
        {
            ENGINE_CORE_ERROR("[CUDA-VK] RegisterBuffer '{0}': CUDA import failed: {1}", slot.Name, e.what());
            vkFreeMemory(m_Impl->Device, slot.Memory, nullptr);
            vkDestroyBuffer(m_Impl->Device, slot.Buffer, nullptr);
            return -1;
        }

        int slotIndex = static_cast<int>(m_Impl->Slots.size());
        m_Impl->Slots.push_back(std::move(slot));

        ENGINE_CORE_INFO("[CUDA-VK] RegisterBuffer '{0}' (slot {1}, size {2} bytes)", debugName ? debugName : "unnamed",
                         slotIndex, sizeInBytes);
        return slotIndex;
    }

    void CudaVulkanInteropContext::UnregisterAll()
    {
        for (auto& slot : m_Impl->Slots)
        {
            if (slot.Registered)
            {
                slot.CudaCtx.Destroy();
                slot.Registered = false;
            }
            if (slot.Memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(m_Impl->Device, slot.Memory, nullptr);
                slot.Memory = VK_NULL_HANDLE;
            }
            if (slot.Buffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(m_Impl->Device, slot.Buffer, nullptr);
                slot.Buffer = VK_NULL_HANDLE;
            }
        }
        m_Impl->Slots.clear();
    }

    // ------------------------------------------------------------------ 映射/取消映射

    bool CudaVulkanInteropContext::MapAll()
    {
        if (CudaInterop::IsCudaPoisoned())
            return false;
        if (m_Impl->Mapped || m_Impl->Slots.empty())
            return false;

        auto syncValues = CudaInterop::BuildInteropFrameSyncValues(m_Impl->FrameIndex);

        // 让所有槽的 CUDA 上下文等待 Vulkan 完成
        try
        {
            for (auto& slot : m_Impl->Slots)
            {
                if (slot.Registered)
                    slot.CudaCtx.WaitTimelineValue(syncValues.WaitVulkanValue);
            }
        }
        catch (const std::exception& e)
        {
            ENGINE_CORE_ERROR("[CUDA-VK] MapAll: wait failed: {0}", e.what());
            CudaInterop::PoisonCuda("CudaVulkanInteropContext MapAll wait failed");
            return false;
        }

        m_Impl->Mapped = true;
        return true;
    }

    void CudaVulkanInteropContext::UnmapAll()
    {
        if (!m_Impl->Mapped)
            return;

        auto syncValues = CudaInterop::BuildInteropFrameSyncValues(m_Impl->FrameIndex);

        // 让所有槽的 CUDA 上下文发出信号
        try
        {
            for (auto& slot : m_Impl->Slots)
            {
                if (slot.Registered)
                {
                    slot.CudaCtx.SyncStream(); // 等待 CUDA 工作完成
                    slot.CudaCtx.SignalTimelineValue(syncValues.CudaSignalValue);
                }
            }
        }
        catch (const std::exception& e)
        {
            ENGINE_CORE_ERROR("[CUDA-VK] UnmapAll: signal failed: {0}", e.what());
            CudaInterop::PoisonCuda("CudaVulkanInteropContext UnmapAll signal failed");
        }

        m_Impl->Mapped = false;
    }

    // ------------------------------------------------------------------ 访问器

    void* CudaVulkanInteropContext::GetMappedPointer(int slot) const
    {
        if (slot < 0 || slot >= static_cast<int>(m_Impl->Slots.size()))
            return nullptr;
        return m_Impl->Slots[slot].CudaCtx.GetMappedDevicePointer();
    }

    void* CudaVulkanInteropContext::GetVulkanBuffer(int slot) const
    {
        if (slot < 0 || slot >= static_cast<int>(m_Impl->Slots.size()))
            return nullptr;
        return m_Impl->Slots[slot].Buffer;
    }

    void* CudaVulkanInteropContext::GetStream() const
    {
        // 返回第一个槽的流，或者共享流
        if (!m_Impl->Slots.empty() && m_Impl->Slots[0].Registered)
            return m_Impl->Slots[0].CudaCtx.GetStream();
        return m_Impl->SharedStream;
    }

    bool CudaVulkanInteropContext::IsMapped() const
    {
        return m_Impl->Mapped;
    }

    int CudaVulkanInteropContext::GetSlotCount() const
    {
        return static_cast<int>(m_Impl->Slots.size());
    }

    CudaInterop::InteropFrameSyncValues CudaVulkanInteropContext::GetCurrentSyncValues() const
    {
        return CudaInterop::BuildInteropFrameSyncValues(m_Impl->FrameIndex);
    }

    void CudaVulkanInteropContext::AdvanceFrame()
    {
        m_Impl->FrameIndex++;
    }

} // namespace Engine
