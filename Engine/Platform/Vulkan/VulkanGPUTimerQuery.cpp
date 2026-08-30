#include "engpch.h"
#include "Platform/Vulkan/VulkanGPUTimerQuery.h"

#include "Core/Log.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Renderer/RendererAPI.h"

#include <array>
#include <cstdlib>

namespace Engine
{

    VulkanGPUTimerQuery::VulkanGPUTimerQuery()
    {
        auto* ctx = VulkanContext::Get();
        if (RendererAPI::GetAPI() != RendererAPI::API::Vulkan || ctx == nullptr)
        {
            m_Disabled = true;
            return;
        }

        const char* disableEnv = std::getenv("ENGINE_DISABLE_GPU_TIMER");
        if (disableEnv && disableEnv[0] == '1')
        {
            m_Disabled = true;
            return;
        }

        // timestamp 支持校验（同 FluidSystemGPU 先例）：compute&graphics 时间戳 +
        // graphics 队列家族 timestampValidBits
        VkPhysicalDeviceProperties deviceProperties{};
        vkGetPhysicalDeviceProperties(ctx->GetPhysicalDevice(), &deviceProperties);

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(ctx->GetPhysicalDevice(), &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(ctx->GetPhysicalDevice(), &queueFamilyCount, queueFamilies.data());

        const uint32_t graphicsQueueFamily = ctx->GetGraphicsQueueFamily();
        if (!deviceProperties.limits.timestampComputeAndGraphics || graphicsQueueFamily >= queueFamilies.size() ||
            queueFamilies[graphicsQueueFamily].timestampValidBits == 0)
        {
            ENGINE_CORE_WARN(
                "[Perf][Vulkan] GPU timestamp query unsupported on active graphics queue, timer disabled.");
            m_Disabled = true;
            return;
        }

        m_TimestampPeriodNs  = deviceProperties.limits.timestampPeriod;
        m_TimestampValidBits = queueFamilies[graphicsQueueFamily].timestampValidBits;

        VkQueryPoolCreateInfo queryInfo{};
        queryInfo.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryInfo.queryType  = VK_QUERY_TYPE_TIMESTAMP;
        queryInfo.queryCount = 2 /*帧槽*/ * kSlotQueryCount;
        if (vkCreateQueryPool(ctx->GetDevice(), &queryInfo, nullptr, &m_QueryPool) != VK_SUCCESS)
        {
            ENGINE_CORE_WARN("[Perf][Vulkan] Failed to create timestamp query pool, timer disabled.");
            m_QueryPool = VK_NULL_HANDLE;
            m_Disabled  = true;
            return;
        }

        m_Device = ctx->GetDevice();
    }

    VulkanGPUTimerQuery::~VulkanGPUTimerQuery()
    {
        // Resources should already be freed by Destroy().
        // Do NOT vkDestroyQueryPool here — the device may be gone.
    }

    void VulkanGPUTimerQuery::Destroy()
    {
        if (m_Disabled || m_QueryPool == VK_NULL_HANDLE)
            return;

        const VkQueryPool pool   = m_QueryPool;
        const VkDevice    device = m_Device;
        m_QueryPool              = VK_NULL_HANDLE;

        if (VulkanContext::Get() != nullptr)
        {
            VulkanContext::DeferDestroy([pool, device](VkDevice) { vkDestroyQueryPool(device, pool, nullptr); });
            return;
        }

        vkDestroyQueryPool(device, pool, nullptr);
    }

    void VulkanGPUTimerQuery::FetchPreviousResult(uint32_t slot, uint32_t prevPairCount)
    {
        // 读上一轮同槽全部 pair（2 帧槽循环 ≈2 帧延迟）。GPU 滞后时可能未就绪：
        // 非阻塞 + AVAILABILITY 查询，未就绪保持旧缓存值（m_HasValidResult=false），
        // 与 GL 实现的 GL_QUERY_RESULT_AVAILABLE 轮询语义一致。
        auto* ctx = VulkanContext::Get();
        if (ctx == nullptr || m_QueryPool == VK_NULL_HANDLE || prevPairCount == 0)
            return;

        const uint32_t                            queryBase = slot * kSlotQueryCount;
        std::array<uint64_t, kSlotQueryCount * 2> data{}; // 每 query [value, availability]
        const VkResult                            result =
            vkGetQueryPoolResults(m_Device, m_QueryPool, queryBase, prevPairCount * 2, sizeof(data), data.data(),
                                  sizeof(uint64_t) * 2, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (result == VK_NOT_READY)
            return;
        if (result != VK_SUCCESS)
        {
            ENGINE_CORE_WARN("[Perf][Vulkan] vkGetQueryPoolResults failed ({0}), skipping sample.",
                             static_cast<int>(result));
            return;
        }

        auto pairAvailable = [&data](uint32_t pair) { return data[pair * 4 + 1] != 0 && data[pair * 4 + 3] != 0; };
        if (!pairAvailable(0))
            return; // 结果未就绪

        // 时长语义（实测 RTX 3050 Ti 得出，见 SPEC D-21）：
        // - 单 pair（Begin/End 均在 render pass 外，如粒子 compute / shadow）：两侧
        //   BOTTOM_OF_PIPE 写入差值 = 区间 GPU 工作时长，直接可用；
        // - 多 pair（编辑器下 GeometryPass 被主渲染 + 拾取各录一次）：render pass 内
        //   的 timestamp 写入被驱动合并到同一时刻（pass 内 delta 恒 0），但不同 pass
        //   的写入锚点有效——用相邻 pair 的 begin 差值 = 主 pass GPU 时长。
        uint64_t elapsedTicks = 0;
        if (prevPairCount >= 2 && pairAvailable(1))
            elapsedTicks = data[1 * 4 + 0] - data[0 * 4 + 0]; // begin[1] - begin[0]
        else
            elapsedTicks = data[0 * 4 + 2] - data[0 * 4 + 0]; // end[0] - begin[0]

        if (m_TimestampValidBits < 64)
        {
            const uint64_t validMask = (uint64_t{1} << m_TimestampValidBits) - 1;
            elapsedTicks &= validMask;
        }

        m_ElapsedMs      = static_cast<float>(static_cast<double>(elapsedTicks) * m_TimestampPeriodNs / 1000000.0);
        m_HasValidResult = true;
    }

    void VulkanGPUTimerQuery::Begin()
    {
        m_HasValidResult = false;

        if (m_Disabled)
            return;

        auto*           ctx = VulkanContext::Get();
        VkCommandBuffer cmd = ctx ? ctx->GetCurrentFrameCommandBuffer() : VK_NULL_HANDLE;
        if (ctx == nullptr || m_QueryPool == VK_NULL_HANDLE || cmd == VK_NULL_HANDLE)
            return; // BeginFrame~EndFrame 录制窗口外无法嵌入时间戳

        const uint64_t frameCounter = ctx->GetFrameCounter();
        const uint32_t slot         = ctx->GetCurrentFrameIndex();

        if (frameCounter != m_LastFrameSeen)
        {
            // 新帧：先读上一轮结果再重置整槽（重置会清除结果，顺序不可颠倒）。
            // 宿主端重置（hostQueryReset 特性）对本帧后续 GPU 写入必然先行——
            // cmd 尚未提交；上一轮写入已在 BeginFrame 等待本槽 fence 时完成。
            const uint32_t prevPairCount = m_FrameCursor[slot];
            FetchPreviousResult(slot, prevPairCount);
            vkResetQueryPool(m_Device, m_QueryPool, slot * kSlotQueryCount, kSlotQueryCount);
            m_FrameCursor[slot] = 0;
            m_LastFrameSeen     = frameCounter;
        }

        if (m_FrameCursor[slot] >= kMaxPairsPerFrame)
            return; // 帧内 query 对耗尽：跳过本对测量（如连续多次重录 pass）

        const uint32_t queryBase = slot * kSlotQueryCount + m_FrameCursor[slot] * 2;
        // Begin/End 两侧都用 BOTTOM_OF_PIPE（"之前所有工作完成"的硬同步点）。
        // 不能用 TOP_OF_PIPE——实测（RTX 3050 Ti）嵌在 render pass 内的 TOP 写入被
        // front-end 提前解析，与 BOTTOM 写入重合，delta 恒 0。pass 内的 BOTTOM 写入
        // 也会被合并到同一时刻（同 pair 内 delta 恒 0），由 FetchPreviousResult 的
        // 多 pair begin 锚差值语义补偿（D-21）。
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_QueryPool, queryBase);
        m_QueryActive = true;
    }

    void VulkanGPUTimerQuery::End()
    {
        if (m_Disabled)
            return;

        auto*           ctx = VulkanContext::Get();
        VkCommandBuffer cmd = ctx ? ctx->GetCurrentFrameCommandBuffer() : VK_NULL_HANDLE;
        if (cmd == VK_NULL_HANDLE || !m_QueryActive)
        {
            m_QueryActive = false;
            return;
        }

        const uint32_t slot = ctx->GetCurrentFrameIndex();
        if (m_FrameCursor[slot] >= kMaxPairsPerFrame)
            return; // 对应 Begin 已跳过

        const uint32_t queryBase = slot * kSlotQueryCount + m_FrameCursor[slot] * 2;
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_QueryPool, queryBase + 1);
        ++m_FrameCursor[slot];
        m_QueryActive = false;
    }

} // namespace Engine
