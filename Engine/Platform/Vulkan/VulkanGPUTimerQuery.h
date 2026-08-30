#pragma once

#include "Debug/GPUTimerQuery.h"

#include <vulkan/vulkan.h>

namespace Engine
{

    // Vulkan 后端 GPUTimerQuery：VkQueryPool(VK_QUERY_TYPE_TIMESTAMP) + 帧槽位复用。
    // 2 帧槽 × 2 query（Begin/End 各一），按 VulkanContext 帧索引 frameIndex*2 定位；
    // Begin 时非阻塞读本槽上次结果（双槽循环 ≈2 帧延迟，GPU 滞后未就绪则保持旧值）。
    // 先例：FluidSystemGPU 自建 timestamp 计时（FluidSystemGPU.cpp Vulkan 分支）。
    class VulkanGPUTimerQuery : public GPUTimerQuery
    {
    public:
        VulkanGPUTimerQuery();
        ~VulkanGPUTimerQuery() override;

        void Destroy() override;
        void Begin() override;
        void End() override;

    private:
        void FetchPreviousResult();

        VkDevice    m_Device             = VK_NULL_HANDLE;
        double      m_TimestampPeriodNs  = 0.0;
        uint32_t    m_TimestampValidBits = 0;
        VkQueryPool m_QueryPool          = VK_NULL_HANDLE;
        bool        m_SlotWritten[2]     = {false, false}; // 槽位是否已有在飞/完成的时间戳
        bool        m_QueryActive        = false;
    };

} // namespace Engine
