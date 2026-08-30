#include "engpch.h"
#include "Platform/Vulkan/VulkanGPUTimerQuery.h"

#include "Core/Log.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Renderer/RendererAPI.h"

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
        queryInfo.queryCount = 2 /*帧槽*/ * 2 /*begin/end*/;
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

    void VulkanGPUTimerQuery::FetchPreviousResult()
    {
        // 本槽上次写入 = 2 帧前（2 帧槽循环）。GPU 滞后时可能未就绪：
        // 非阻塞 + AVAILABILITY 查询，未就绪保持旧缓存值（m_HasValidResult=false），
        // 与 GL 实现的 GL_QUERY_RESULT_AVAILABLE 轮询语义一致。
        auto* ctx = VulkanContext::Get();
        if (ctx == nullptr || m_QueryPool == VK_NULL_HANDLE)
            return;

        const uint32_t frameIndex = ctx->GetCurrentFrameIndex();
        if (!m_SlotWritten[frameIndex])
            return;

        const uint32_t queryBase = frameIndex * 2;
        uint64_t       data[4]   = {}; // [begin, beginAvail, end, endAvail]
        const VkResult result =
            vkGetQueryPoolResults(m_Device, m_QueryPool, queryBase, 2, sizeof(data), data, sizeof(uint64_t) * 2,
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (result == VK_NOT_READY)
            return;
        if (result != VK_SUCCESS)
        {
            ENGINE_CORE_WARN("[Perf][Vulkan] vkGetQueryPoolResults failed ({0}), skipping sample.",
                             static_cast<int>(result));
            return;
        }
        if (data[1] == 0 || data[3] == 0)
            return; // 可用位为 0：结果未就绪

        uint64_t elapsedTicks = data[2] - data[0];
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
        if (cmd == VK_NULL_HANDLE)
            return; // BeginFrame~EndFrame 录制窗口外无法嵌入时间戳

        FetchPreviousResult();

        const uint32_t queryBase = ctx->GetCurrentFrameIndex() * 2;
        vkCmdResetQueryPool(cmd, m_QueryPool, queryBase, 2);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_QueryPool, queryBase);
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

        const uint32_t queryBase = ctx->GetCurrentFrameIndex() * 2;
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_QueryPool, queryBase + 1);
        m_SlotWritten[ctx->GetCurrentFrameIndex()] = true;
        m_QueryActive                              = false;
    }

} // namespace Engine
