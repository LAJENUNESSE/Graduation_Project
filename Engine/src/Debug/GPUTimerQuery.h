#pragma once

#include "Core/Base.h"

#include <cstdint>

namespace Engine
{

    // Double-buffered GPU timer query to avoid pipeline stalls.
    // Frame N: Begin/End writes to query[current].
    // Frame N+1: Begin reads query[previous] result, then starts new query.
    // Result has 1-frame latency but zero stall.
    //
    // 抽象基类：具体实现按 RendererAPI 工厂分派（GPUAsyncReadback::Create 同款模式）
    //   OpenGL → OpenGLGPUTimerQuery（Platform/OpenGL，glBeginQuery GL_TIME_ELAPSED）
    //   Vulkan → VulkanGPUTimerQuery（Platform/Vulkan，VkQueryPool TIMESTAMP）
    class GPUTimerQuery
    {
    public:
        virtual ~GPUTimerQuery() = default;

        // Release backend resources explicitly (must be called while the graphics
        // context is alive). 释放后端资源，须在图形上下文存活时调用。
        virtual void Destroy() = 0;

        // Call Begin before the GPU work you want to measure.
        // Internally reads the previous frame's result first.
        virtual void Begin() = 0;

        // Call End after the GPU work.
        virtual void End() = 0;

        // Returns the GPU elapsed time in milliseconds (from the previous frame).
        float GetElapsedMs() const { return m_ElapsedMs; }
        bool  HasValidResult() const { return m_HasValidResult; }
        void  SetBlockingReadback(bool enabled) { m_BlockingReadback = enabled; }

        // Non-copyable
        GPUTimerQuery(const GPUTimerQuery&)            = delete;
        GPUTimerQuery& operator=(const GPUTimerQuery&) = delete;

        // 工厂：按当前 RendererAPI 分派后端实现（单例 PerformanceMonitor 构造时创建）
        static Ref<GPUTimerQuery> Create();

    protected:
        GPUTimerQuery() = default;

        float m_ElapsedMs        = 0.0f;
        bool  m_Disabled         = false; // Disabled on API mismatch / unstable drivers / env override
        bool  m_HasValidResult   = false; // Begin() fetched a new result for this frame
        bool  m_BlockingReadback = false; // Offline benchmark: never overwrite an unread query slot
    };

} // namespace Engine
