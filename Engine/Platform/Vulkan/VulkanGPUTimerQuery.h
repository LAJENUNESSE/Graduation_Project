#pragma once

#include "Debug/GPUTimerQuery.h"

#include <vulkan/vulkan.h>

namespace Engine
{

    // Vulkan 后端 GPUTimerQuery：VkQueryPool(VK_QUERY_TYPE_TIMESTAMP) + 帧槽位复用。
    //
    // 布局：2 帧槽 × 每槽 kMaxPairsPerFrame 对 × 2 query（Begin/End 各一）。
    // 每帧首对 Begin 做一次宿主端 vkResetQueryPool 重置整槽（hostQueryReset 特性），
    // 同帧后续 Begin/End 对经帧内游标使用不同 query 对——两项设计的原因：
    //   1. vkCmdResetQueryPool 不得录制进 render pass，而计时器调用点嵌在
    //      HDR FBO 的 render pass 内（VulkanFramebuffer::Bind 即 BeginRenderPass），
    //      cmd 重置会被 validation 拒绝；
    //   2. "query uses 之间必须 reset"：同帧多对（编辑器主渲染 + 拾取重录
    //      GeometryPass）写同一对 query 会触发 VUID，必须用不同 query 对。
    // 每对 Begin 时非阻塞读上一轮同槽 pair 0 的结果（双槽循环约 2 帧延迟，
    // 未就绪保持旧值）。结构先例：FluidSystemGPU 自建 timestamp 计时。
    class VulkanGPUTimerQuery : public GPUTimerQuery
    {
    public:
        VulkanGPUTimerQuery();
        ~VulkanGPUTimerQuery() override;

        void Destroy() override;
        void Begin() override;
        void End() override;

    private:
        void FetchPreviousResult(uint32_t slot, uint32_t prevPairCount);

        static constexpr uint32_t kMaxPairsPerFrame = 4;
        static constexpr uint32_t kSlotQueryCount   = kMaxPairsPerFrame * 2;

        VkDevice    m_Device             = VK_NULL_HANDLE;
        double      m_TimestampPeriodNs  = 0.0;
        uint32_t    m_TimestampValidBits = 0;
        VkQueryPool m_QueryPool          = VK_NULL_HANDLE;
        uint64_t    m_LastFrameSeen      = 0;      // 帧内游标复位标记
        uint32_t    m_FrameCursor[2]     = {0, 0}; // 本帧已占用 query 对数（按帧槽）
        bool        m_QueryActive        = false;
    };

} // namespace Engine
